#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>

using namespace std;

int n;
vector<int> arr;
vector<int> original;
int segLen[8];
int segStartIdx[9];
int parentJob[15];
int firstSegIdx[15];
int lastSegIdx[15];
bool jobDone[15];
int currentThreadCount;
bool tasksComplete;

pthread_mutex_t queueMutex;
sem_t jobAvailable;
queue<int> jobQueue;

void bubble(int segIndex) {
    int start = segStartIdx[segIndex];
    int end   = segStartIdx[segIndex + 1];
    for (int i = start; i < end - 1; ++i) {
        for (int j = i + 1; j < end; ++j) {
            if (arr[i] > arr[j]) {
                int temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
    }
}

void merge(int start, int mid, int end) {
    int i = start;
    int j = mid;
    vector<int> temp;
    temp.reserve(end - start);
    
    while (i < mid && j < end) {
        if (arr[i] <= arr[j]) {
            temp.push_back(arr[i++]);
        } else {
            temp.push_back(arr[j++]);
        }
    }
    while (i < mid) {
        temp.push_back(arr[i++]);
    }
    while (j < end) {
        temp.push_back(arr[j++]);
    }
    for (int k = 0; k < (int)temp.size(); ++k) {
        arr[start + k] = temp[k];
    }
}

void mergeSort(int jobId) {
    int leftSeg = firstSegIdx[jobId];
    int rightSeg = lastSegIdx[jobId];
    
    int midSeg = (leftSeg + rightSeg + 1) / 2;
    int startIdx = segStartIdx[leftSeg];
    int midIdx   = segStartIdx[midSeg];
    int endIdx   = segStartIdx[rightSeg + 1];
    merge(startIdx, midIdx, endIdx);
}

void* workerFunc(void* arg) {
    while (true) {
        sem_wait(&jobAvailable);
        pthread_mutex_lock(&queueMutex);
        if (jobQueue.empty()) {
            pthread_mutex_unlock(&queueMutex);
            if (tasksComplete) {
                break;
            } else {
                continue;
            }
        }
        int jobId = jobQueue.front();
        jobQueue.pop();
        pthread_mutex_unlock(&queueMutex);

        if (jobId < 8) {
            bubble(jobId);
        } else {
            mergeSort(jobId);
        }

        if (jobId < 14) {
            pthread_mutex_lock(&queueMutex);
            jobDone[jobId] = true;
            int buddyId = (jobId % 2 == 0) ? jobId + 1 : jobId - 1;
            if (jobDone[buddyId]) {
                int parentId = parentJob[jobId];
                jobQueue.push(parentId);
                jobDone[parentId] = false;
                sem_post(&jobAvailable);
            }
            pthread_mutex_unlock(&queueMutex);
        } else {
            pthread_mutex_lock(&queueMutex);
            tasksComplete = true;
            pthread_mutex_unlock(&queueMutex);
            for (int k = 0; k < currentThreadCount - 1; ++k) {
                sem_post(&jobAvailable);
            }
            break;
        }
    }
    return NULL;
}

int main() {
    ifstream input("input.txt");
    if (!input) {
        cerr << "Error: could not open input.txt\n";
        return 1;
    }
    input >> n;
    arr.resize(n);
    for (int i = 0; i < n; ++i) {
        input >> arr[i];
    }
    input.close();
    original = arr;

    int baseLen = n / 8;
    int remainder = n % 8;
    for (int i = 0; i < 8; ++i) {
        segLen[i] = baseLen + (i < remainder ? 1 : 0);
    }
    
    segStartIdx[0] = 0;
    for (int i = 1; i <= 8; ++i) {
        segStartIdx[i] = segStartIdx[i-1] + (i-1 < 8 ? segLen[i-1] : 0);
    }

    for (int i = 0; i < 8; ++i) {
        firstSegIdx[i] = i;
        lastSegIdx[i] = i;
    }
    // First merge
    parentJob[0] = parentJob[1] = 8;
    firstSegIdx[8] = 0;
    lastSegIdx[8] = 1;
    parentJob[2] = parentJob[3] = 9;
    firstSegIdx[9] = 2;
    lastSegIdx[9] = 3;
    parentJob[4] = parentJob[5] = 10;
    firstSegIdx[10] = 4;
    lastSegIdx[10] = 5;
    parentJob[6] = parentJob[7] = 11;
    firstSegIdx[11] = 6;
    lastSegIdx[11] = 7;
    // Second merge
    parentJob[8]  = parentJob[9]  = 12;
    firstSegIdx[12] = 0;
    lastSegIdx[12]  = 3;
    parentJob[10] = parentJob[11] = 13;
    firstSegIdx[13] = 4;
    lastSegIdx[13]  = 7;
    // Third (Final) merge
    parentJob[12] = parentJob[13] = 14;
    firstSegIdx[14] = 0;
    lastSegIdx[14]  = 7;
    parentJob[14] = -1;

    pthread_mutex_init(&queueMutex, NULL);
    sem_init(&jobAvailable, 0, 0);

    struct timeval start_time, end_time;
    ofstream outputFile;

    for (int t = 1; t <= 8; ++t) {
        currentThreadCount = t;
        // Reset array and flags
        arr = original;
        for (int j = 0; j < 15; ++j) {
            jobDone[j] = false;
        }
        tasksComplete = false;
        while (!jobQueue.empty()) {
            jobQueue.pop();
        }

        gettimeofday(&start_time, NULL);

        // Enqueue initial 8 bubble-sort jobs
        for (int jobId = 0; jobId < 8; ++jobId) {
            jobQueue.push(jobId);
            sem_post(&jobAvailable);
        }
        vector<pthread_t> threads(t);
        for (int i = 0; i < t; ++i) {
            pthread_create(&threads[i], NULL, workerFunc, NULL);
        }
        for (int i = 0; i < t; ++i) {
            pthread_join(threads[i], NULL);
        }

        gettimeofday(&end_time, NULL);
        double elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                             (end_time.tv_usec - start_time.tv_usec) / 1000.0;
        printf("worker thread #%d, elapsed %.3f ms\n", t, elapsed_ms);

        string outFileName = "output_" + to_string(t) + ".txt";
        outputFile.open(outFileName);
        if (outputFile.is_open()) {
            for (int i = 0; i < n; ++i) {
                outputFile << arr[i];
                if (i < n - 1) outputFile << " ";
            }
            outputFile.close();
        } else {
            cerr << "Error: could not open " << outFileName << " for writing\n";
        }
    }
    pthread_mutex_destroy(&queueMutex);
    sem_destroy(&jobAvailable);
    return 0;
}
