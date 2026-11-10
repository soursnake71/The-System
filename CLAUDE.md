# Working rules for this repository

- **Code in C++.** New engines, tools and apps are written in C++ (C++17, CMake). Other languages only where a
  platform forces it (Android UI glue in Java, the web pages in JS) or for one-off offline tools (e.g. exporting a
  model to ONNX). The Drum Scribe engine (`drumscribe/core`) is C++; `drumscribe/engine` (Python) is only the
  reference prototype the C++ is checked against.
- Precision first: every analysis module has a test against exact ground truth (synthetic signals with known answers)
  and the numbers go in the module's README section.
