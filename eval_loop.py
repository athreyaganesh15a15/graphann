import subprocess
import re
import json

def calculate_fitness(recall, latency):
    # Base fitness: multiply recall to make it dominant
    fitness = recall * 10000 
    
    # Heavily reward Recall > 0.99
    if recall > 0.99:
        fitness += 5000
        
    # Penalize high latency
    fitness -= latency * 0.1
    
    return fitness

def main():
    print("Building project...")
    subprocess.run(["cmake", "--build", "build", "-j4"], check=True)

    print("Running benchmark...")
    process = subprocess.Popen(
        ["bash", "scripts/run_sift1m.sh"], 
        stdout=subprocess.PIPE, 
        stderr=subprocess.STDOUT,
        text=True
    )

    best_recall = 0.0
    best_latency = 0.0
    best_fitness = -float('inf')

    # Regex to match the table rows
    #        L     Recall@10   Avg Dist Cmps  Avg Latency (us)  P99 Latency (us)
    #      200        0.9965          4217.6            3848.1           11515.0
    row_pattern = re.compile(r"^\s*\d+\s+([0-9\.]+)\s+[0-9\.]+\s+[0-9\.]+\s+([0-9\.]+)\s*$")

    for line in process.stdout:
        # We can also print the line if we want to see it in real time
        print(line, end="")
        match = row_pattern.match(line)
        if match:
            recall = float(match.group(1))
            latency = float(match.group(2))
            fitness = calculate_fitness(recall, latency)
            
            # Keep the one with the highest fitness
            if fitness > best_fitness:
                best_fitness = fitness
                best_recall = recall
                best_latency = latency

    result = {
        "recall": best_recall,
        "p99_latency": best_latency,
        "fitness_score": best_fitness
    }
    
    print("\n" + json.dumps(result))

if __name__ == "__main__":
    main()
