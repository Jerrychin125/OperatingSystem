import random

def generate_random_array(size, min_val, max_val):
    return [random.randint(min_val, max_val) for _ in range(size)]

# Example usage
if __name__ == "__main__":
    # Parameters
    array_size = int(1e5)
    minValue = int(-1e9 - 7)
    maxValue = int(1e9 + 7)
    
    # Generate random array
    random_array = generate_random_array(array_size, minValue, maxValue)
    
    # Print the result
    # Write to file
    with open('input.txt', 'w') as f:
        f.write(f"{int(array_size)}\n")
        f.write(' '.join(map(str, random_array)))