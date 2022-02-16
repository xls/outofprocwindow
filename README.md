# outofprocwindow
naïve sample app to test out of proc opengl and the consequences of terminating processes with allocated opengl/GDI resources.

the test application will spawn N number of processes that attaches to the main window; from which each will allocate a number of textures of specified size and render a cube while binding and enumerating all textures allocated. These processes will be terminated at the interval set by respawn time (in seconds).

Default configurations are 3 process, 10 texture of each 4096x4096x4 bytes (square 4096px RGBA) and killed/respawn at an interval of 3seconds.

prebuilt binaries available in [Releases](https://github.com/xls/outofprocwindow/releases)



```
	USAGE: example [options]

 Options:
	[opt]  -h,--help           Print usage and exit.
	[opt]  --count, -c         number of processes (default: 5).
	[opt]  --respawn, -r       respawn time in seconds (default: 3).
	[opt]  --textures, -t      texture count (default: 10).
	[opt]  --size, -s          texture width (default: 4096).
  ```
  
  Can simply be run without parameters for aprox 2gb of vram and 3 instances of out of process gl renders.
  
  results:
  
  ![image](https://user-images.githubusercontent.com/423484/154269641-a4af311e-3095-4133-a32d-83475dfa2b6d.png)
