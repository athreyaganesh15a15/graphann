import subprocess
import re
import json
import sys

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
    print("Running benchmark script...")
    # Using run instead of Popen so we can just wait for it and then parse
    process = subprocess.Popen(
        ["bash", "scripts/run_sift1m_full.sh", "--r64"], 
        stdout=subprocess.PIPE, 
        stderr=subprocess.STDOUT,
        text=True
    )

    best_recall = 0.0
    best_latency = 0.0
    best_fitness = -float('inf')

    # States for parsing
    in_target_section = False
    in_table = False

    row_pattern = re.compile(r"^\s*\d+\s+([0-9\.]+)\s+[0-9\.]+\s+[0-9\.]+\s+([0-9\.]+)\s*$")

    for line in process.stdout:
        print(line, end="")
        sys.stdout.flush()
        
        if "=== R=64 Quantized + Dynamic Beam (Optimizations A+C) ===" in line:
            in_target_section = True
            
        if in_target_section and "=== Search Results (" in line:
            in_table = True
            continue
            
        if in_table:
            if "Done." in line or "===" in line:
                in_table = False
                in_target_section = False
                continue
                
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
