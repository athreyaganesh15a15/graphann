import sys
import re
import subprocess
import json
import shutil

HEURISTICS = {
    "H0": """            if (dist_to_node > alpha * dist_cand_to_selected) {
                keep = false;
                break;
            }""",
            
    "H1": """            float local_alpha = alpha;
            if (dist_cand_to_selected < 0.15f * dist_to_node) {
                local_alpha = alpha * 0.85f;
            }
            if (dist_to_node > local_alpha * dist_cand_to_selected) {
                keep = false;
                break;
            }""",
            
    "H2": """            float ratio = dist_cand_to_selected / (dist_to_node + 1e-6f);
            float dynamic_alpha = alpha * (0.9f + 0.2f * std::min(1.0f, ratio));
            if (dist_to_node > dynamic_alpha * dist_cand_to_selected) {
                keep = false;
                break;
            }""",
            
    "H3": """            float penalty = 0.05f * dist_to_node;
            if (dist_to_node > alpha * dist_cand_to_selected + penalty) {
                keep = false;
                break;
            }""",
            
    "H4": """            float ratio = dist_cand_to_selected / (dist_to_node + 1e-6f);
            float dynamic_alpha = alpha * (0.95f + 0.1f * std::min(1.0f, ratio));
            if (dist_to_node > dynamic_alpha * dist_cand_to_selected) {
                keep = false;
                break;
            }"""
}

def inject_cpp(heuristic):
    cpp_file = "src/vamana_index.cpp"
    with open(cpp_file, "r") as f:
        content = f.read()
    
    # Locate the exact compute_l2sq block up to the break; }
    # This assumes the code was restored to H0 clean state initially.
    pattern = re.compile(r"(compute_l2sq\(get_vector\(cand_id\), get_vector\(selected\), dim_\);\s*\n).*?(keep = false;\s*\n\s*break;\s*\n\s*\})", re.DOTALL)
    
    def replacer(match):
        return match.group(1) + HEURISTICS[heuristic]
    
    new_content = pattern.sub(replacer, content)
    
    with open(cpp_file, "w") as f:
        f.write(new_content)

def inject_sh(alpha, gamma):
    sh_file = "scripts/run_sift1m_full.sh"
    with open(sh_file, "r") as f:
        content = f.read()
        
    pattern = re.compile(r"--R 32 --L 75 --alpha [0-9\.]+ --gamma [0-9\.]+")
    new_str = f"--R 32 --L 75 --alpha {alpha} --gamma {gamma}"
    new_content = pattern.sub(new_str, content)
    
    with open(sh_file, "w") as f:
        f.write(new_content)

def run_bench():
    print("Building project...")
    subprocess.run(["cmake", "--build", "build", "-j4"], check=True)
    
    print("Running benchmark...")
    process = subprocess.Popen(
        ["bash", "scripts/run_sift1m_full.sh", "--r32"], 
        stdout=subprocess.PIPE, 
        stderr=subprocess.STDOUT,
        text=True
    )
    
    in_table = False
    row_pattern = re.compile(r"^\s*\d+\s+([0-9\.]+)\s+[0-9\.]+\s+[0-9\.]+\s+([0-9\.]+)\s*$")
    
    results = {}
    for line in process.stdout:
        print(line, end="")
        sys.stdout.flush()
        
        # We only care about the Exact Float32 Search for baseline comparisons
        if "=== Search Results (K=10) ===" in line:
            in_table = True
            
        if "=== R=32 Quantized" in line:
            in_table = False
            
        if in_table:
            if "Done." in line or "===" in line:
                in_table = False
                
            match = row_pattern.match(line)
            if match:
                parts = line.split()
                l_val = int(parts[0])
                recall = float(parts[1])
                latency = float(parts[4])
                
                if l_val in [50, 100]:
                    results[l_val] = {"recall": recall, "latency": latency}
                    
    return results

def main():
    if len(sys.argv) != 4:
        print("Usage: python3 grid_search.py <heuristic> <alpha> <gamma>")
        sys.exit(1)
        
    heuristic = sys.argv[1]
    alpha = float(sys.argv[2])
    gamma = float(sys.argv[3])
    
    # First revert back to clean state so regex always matches
    inject_cpp("H0")
    
    # Inject current configuration
    inject_cpp(heuristic)
    inject_sh(alpha, gamma)
    
    res = run_bench()
    print(f"\nRESULTS for {heuristic}, alpha={alpha}, gamma={gamma}:")
    print(res)
    
    with open("grid_results.txt", "a") as f:
        f.write(f"{heuristic}, {alpha}, {gamma} -> {res}\n")

if __name__ == "__main__":
    main()
