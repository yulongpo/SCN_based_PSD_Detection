# Local vendor dependency layout

This directory is populated by the SCN dependency release bundle.

The source tree expects the following layout at the project root:

```text
third_party/
├── bb60c/
├── harogic/
└── tensorrt/
```

The bundle contains vendor import libraries and runtime DLLs. Qt and CUDA are
not included; install them separately according to the root `Readme.md`.

Download the dependency ZIP from the GitHub Releases page and extract it at
the repository root, preserving the `third_party/` and `models/` directories.
