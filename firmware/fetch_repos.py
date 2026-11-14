import os
import subprocess
import json


def clone_or_update_repo(
    repo_url, path, ref=None, with_submodules=False, patch_path=None
):
    if not os.path.exists(path):
        subprocess.run(["git", "clone", repo_url, path], check=True)
    if ref:
        available = subprocess.run(
            ["git", "-C", path, "cat-file", "-e", f"{ref}^{{commit}}"],
            capture_output=True,
        )
        if available.returncode != 0:
            subprocess.run(["git", "-C", path, "fetch", "origin", ref], check=True)
        current = subprocess.check_output(
            ["git", "-C", path, "rev-parse", "HEAD"], text=True
        ).strip()
        if current != ref:
            subprocess.run(["git", "-C", path, "checkout", ref], check=True)

    if with_submodules:
        subprocess.run(
            ["git", "-C", path, "submodule", "update", "--init", "--recursive"],
            check=True,
        )

    if patch_path:
        patch_full_path = (
            patch_path
            if os.path.isabs(patch_path)
            else os.path.join(os.getcwd(), patch_path)
        )
        check_result = subprocess.run(
            ["git", "-C", path, "apply", "--check", patch_full_path],
            capture_output=True,
        )
        if check_result.returncode == 0:
            subprocess.run(["git", "-C", path, "apply", patch_full_path], check=True)
            print(f"Applied patch {patch_path} to {path}")
        else:
            reverse_result = subprocess.run(
                ["git", "-C", path, "apply", "--reverse", "--check", patch_full_path],
                capture_output=True,
            )
            if reverse_result.returncode != 0:
                raise RuntimeError(
                    f"Patch {patch_path} neither applies cleanly nor is fully applied "
                    f"in {path}. Resolve the dependency changes before building."
                )
            print(f"Patch {patch_path} is already applied to {path}")


def fetch_dependencies():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "repos.json")

    with open(config_path) as f:
        repos = json.load(f)

    for repo in repos:
        repo_path = os.path.join(script_dir, repo["path"])
        ref = repo["ref"]
        with_submodules = repo.get("with_submodules", False)
        patch = repo.get("patch")
        if patch and not os.path.isabs(patch):
            patch = os.path.join(script_dir, patch)
        clone_or_update_repo(repo["url"], repo_path, ref, with_submodules, patch)


if __name__ == "__main__":
    fetch_dependencies()
