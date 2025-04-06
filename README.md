# License

This project is licensed under the [GNU Lesser General Public License v3.0](https://www.gnu.org/licenses/lgpl-3.0.html).


# libvictor

**libvictor** is the core library for managing and operating a vector-based database. It provides efficient algorithms for vector operations, memory management, and modular design, making it suitable for integration into larger projects.

---

## Description

`libvictor` is a foundational library for a vector-based database. It provides the essential components required for efficient vector storage, retrieval, and computation. The library is designed to be modular, allowing easy integration into larger systems.

Key features include:
- **Vector Operations**: Perform various vector computations with optimized algorithms.
- **Memory Management**: Efficient handling of memory for vector data structures.
- **Cross-Platform Support**: Compatible with Linux, macOS, and Windows.

---

## How to Compile

### Prerequisites

- **Compiler**: Ensure you have `gcc` or any compatible C compiler installed.
- **Make**: Required for building the library using the provided Makefiles.

### Steps to Compile

1. **Clone the Repository**:
   ```bash
   git clone https://github.com/victor-base/libvictor.git
   cd libvictor
   ```

2. **Build the library**:
   - For Linux/macOS:
     ```bash
     make
     ```
   - For Windows:
     ```bash
     make -f src/Makefile.win
     ```

---

## How to Install

Currently, the installation process is manual. Follow these steps:

1. **Compile the library** (see "How to Compile").
2. **Copy the compiled library** to your desired location:
   - On Linux/macOS:
     ```bash
     sudo cp libvictor.a /usr/local/lib/
     sudo cp -r include/ /usr/local/include/libvictor/
     ```
   - On Windows:
     Copy the compiled `.lib` or `.dll` files to your project directory or a system-wide library path.

> **Note**: An automated installer is under development and will be included in future releases.

---

## How it Works

`libvictor` provides a set of APIs for managing vector-based data. Here's a high-level overview of how it works:

1. **Initialization**:
   - Set up the vector database and configure parameters such as dimensions and indexing methods.

2. **Vector Insertion**:
   - Insert vectors into the database using efficient memory management.

3. **Vector Search**:
   - Perform similarity searches (e.g., nearest neighbor) using optimized algorithms.

4. **Customization**:
   - Extend the library by implementing custom indexing or similarity metrics.

Refer to the source files in the `src/` directory for detailed implementations.

---

## How to Uninstall

To remove the library manually:

1. **Delete the compiled files**:
   - On Linux/macOS:
     ```bash
     sudo rm /usr/local/lib/libvictor.a
     sudo rm -r /usr/local/include/libvictor/
     ```
   - On Windows:
     Delete the `.lib` or `.dll` files from your system.

2. **Clean the build directory**:
   ```bash
   make clean
   ```

---

## Repository Structure

This repository contains the core files for the vector database:

- **Makefiles**:
  - `Makefile`: For building on Linux/macOS.
  - `Makefile.win`: For building on Windows.
- **Source Files**: Located in the `src/` directory, including implementations for vector operations, memory management, and indexing.
- **Configuration Files**: Found in `.vscode/` for development environment setup.

---

This repository serves as the foundation for the vector database, providing the essential components required for efficient vector storage and retrieval.