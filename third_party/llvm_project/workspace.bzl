"""Provides the repository macro to import LLVM."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
load("//third_party:repo.bzl", "tf_http_archive")

def clean_dep(dep):
    return str(Label(dep))

def repo(name):
    """Imports LLVM."""
    LLVM_COMMIT = "75e33f71c2dae584b13a7d1186ae0a038ba98838"
    LLVM_SHA256 = "9e2ef2fac7525a77220742a3384cafe7a35adc7e5c9750378b2cf25c2d2933f5"

    # tf_http_archive(
    #     name = name,
    #     sha256 = LLVM_SHA256,
    #     strip_prefix = "llvm-project-{commit}".format(commit = LLVM_COMMIT),
    #     urls = [
    #         "https://storage.googleapis.com/mirror.tensorflow.org/github.com/llvm/llvm-project/archive/{commit}.tar.gz".format(commit = LLVM_COMMIT),
    #         "https://github.com/llvm/llvm-project/archive/{commit}.tar.gz".format(commit = LLVM_COMMIT),
    #     ],
    #     build_file = "//third_party/llvm:llvm.BUILD",
    #     # patch_file = ["//third_party/llvm:macos_build_fix.patch"],
    #     # link_files = {"//third_party/llvm:run_lit.sh": "mlir/run_lit.sh"},
    # )

    new_git_repository(
        name = name,
        # branch llvmorg-13.0.1
        commit = LLVM_COMMIT,
        remote = "https://github.com/llvm/llvm-project.git",
        build_file = clean_dep("//third_party/llvm_project:llvm.BUILD"),
        verbose = True,
        # Based on commit https://github.com/llvm/llvm-project/commit/81d5412439efd0860c0a8dd51b831204f118d485
        # The patch change is already in LLVM master but not included in LLVM-13 branch
        # Without this commit, we encounter issue in locating overlay_script.py, which path is hardcoded to "repo/overlay_directories.py".The correct path is  "repo/utils/bazel/overlay_directories.py"
        patches = ["//third_party/llvm_project:simplify_llvm_build.patch"],
        patch_args = ["-p1"],
        patch_cmds = [
            "git log -1 --format=%H > COMMIT",
        ],
    )
