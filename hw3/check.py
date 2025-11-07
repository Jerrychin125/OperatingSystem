def read_file(filename):
    try:
        with open(filename, 'r') as f:
            return f.read()
    except FileNotFoundError:
        print(f"Error: {filename} not found")
        return None

def main():
    # Read all files
    contents = []
    for i in range(1, 9):
        content = read_file(f"output_{i}.txt")
        if content is None:
            return
        contents.append(content)

    # Compare all files with the first one
    all_same = True
    for i in range(1, len(contents)):
        if contents[i] != contents[0]:
            print(f"output_{i+1}.txt is different from output_1.txt")
            all_same = False

    if all_same:
        print("All files are identical")
    
if __name__ == "__main__":
    main()