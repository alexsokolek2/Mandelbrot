Draws the Mandelbrot set. Supports click to change orgin, drag
to zoom in, and mouse wheel zoom in and out about a point. The
user can hold the shift key down while dragging the window
borders to suppress the delay for repainting along the way.
Supports backtracking the axis coordinate range via right click.

Also, saves and restores the WindowPlacement, in the registry,
between executions.

Slices up the graphics workload between 12 threads. The machine
used for development and testing has 10 cores, and 12 logical
processors, hence the choice of 12 threads. We can specify a
somewhat arbitrary number of threads, and performance vs the
number of threads is a little erratic, possibly due to latency
and/or overhead in the OS. I am seeing a factor of 8 to 10
improvement with 12 threads.

The number of slices defaults to 5000. In the first version
the number of slices (12) and the number of threads was the
same. As the threads run at different speeds, due to the
amount of black space per slice, the current version uses a
thread pool where each thread iterates while there is more
work to do. The WorkQueue class facilitates this pool. This
way, performance is maximized as no thread completes until
all threads are completed. Each slice is assigned to the
next thread in the pool. When the thread finishes with that
slice, it getes a new slice from the work queue. All threads
in the pool compete for slices on a first come first serve
basis. When a thread attempts to get a new slice and there
are no more, the thread exits. When all threads exit, the
bitmap is painted to the screen.
 
Also, I implemented an HSV/RGB color scheme with a triple log
scaling factor to get better color results. This is courtesy
of the Windows 11 Copilot (Preview), which was an immense help.
The user is able to select the RGB system or the HSV system.

The number of slices is adjustable, as are the X and Y axes
ranges. The user can choose to see the axes on the final plot.
The max iterations can also be adjusted, at the cost of speed.

The user can save and restore the plot parameters to and from
a file using standard File Save and File Open sequences.

Compilation requires that UNICODE be defined. Some of the
choices made in code, mainly wstring, do not support detecting
UNICODE vs non-UNICODE, so don't compile without UNICODE defined.

In case you get linker errors, be sure to include version.lib
in the linker input line. (This should not be an issue if you
start with a clone of the repository, which will copy the
correct Project Properties.)
 
Microsoft Visual Studio 2022 Community Edition 64 Bit 17.9.5

Alex Sokolek, Version 1.0.0.1, Copyright (c) March 27, 2024
 
Version 1.0.0.2 - April 1, 2024 - Added worker thread pool.

Version 1.0.0.3 - April 17, 2024 - Updated version for release.

Version 1.0.0.4 - April 20, 2024 - Updated comments.

Version 1.0.0.5 - April 21, 2024 - Limited mouse capture range.
