# Dynamic Memory Allocator
 An allocator for the x86-64 architecture
 
## Features
- **Free lists segregated by size class, using first-fit policy within each size class,
augmented with a set of "quick lists" holding small blocks segregated by size.**
- **Immediate coalescing of large blocks on free with adjacent free blocks;
delayed coalescing on free of small blocks.**
- **Boundary tags to support efficient coalescing, with footer optimization that allows
footers to be omitted from allocated blocks.**
- **Block splitting without creating splinters.**
- **Allocated blocks aligned to "double memory row" (16-byte) boundaries.**
- **Free lists maintained using last in first out (LIFO) discipline.**
- **Use of a prologue and epilogue to avoid edge cases at the end of the heap.**
- **Obfuscation of block headers and footers to detect heap corruption and attempts to
free blocks not previously obtained via allocation.**
