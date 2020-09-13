# CGO/DGO Files
The CGO/DGO file format is exactly the same - the only difference is the name of the file.  The DGO name indicates that the file contains all the data for a level. The engine will load these files into a level heap, which can then be cleared and replaced with a different level.  

I suspect that the DGO file name came first, as a package containing all the data in the level which can be loaded very quickly.  Names in the code all say `dgo`, and the `MakeFileName` system shows that both CGO and DGO files are stored in the `game/dgo` folder.  Probably the engine and kernel were packed into a CGO file after the file format was created for loading levels.

Each CGO/DGO file contains a bunch of individual object files.  Each file has a name.  There are some duplicate names - sometimes the two files with the same names are very different (example, code for an enemy, art for an enemy), and other times they are very similar (tiny differences in code/data). The files come in two versions, v4 and v3, and both CGOs and DGOs contain both versions.  If an object file has code in it, it is always a v3.  It is possible to have a v3 file with just data, but usually the data is pretty small. The v4 files tend to have a lot of data in them.  My theory is that the compiler creates v3 files out of GOAL source code files, and that other tools for creating things like textures/levels/art-groups generate v4 objects.  There are a number of optimizations in the loading process for v4 objects that are better suited for larger files.  To stay at 60 fps always, a v3 object must be smaller than around 750 kB. A v4 object does not have this limitation. 
 
# The V3 format
The v3 format is divided into three segments:
1. Main: this contains all of the functions/data that will be used by the game.
2. Debug: this is only loaded in debug mode, and is always stored on a separate `kdebugheap`.
3. Top Level: this contains some initialization code to add functions/variables to the symbol table, and any user-written initialization.  It runs once, immediately after the object is loaded, then is thrown away.

Each segment also has linking data, which tells the linker how to link references to symbols, types, and memory (possibly in a different segment).

This format will be different between the PC and PS2 versions, as linking data for x86-64 will need to look different from MIPS.

Each segments can contain functions and data. The top-level segment must start with a function which will be run to initialize the object.  All the data here goes through the GOAL compiler and type system.

# The V4 format
The V4 format contains just data. Like v3, the data is GOAL objects, but was probably generated by a tool that wasn't the compiler. A V4 object has no segments, but must start with a `basic` object.  After being linked, the `relocate` method of this `basic` will be called, which should do any additional linking required for the specific object.

Because this is just data, there's no reason for the PC version to change this format. This means we can also check the 

Note: you may see references to v2 format in the code. I believe v4 format is identical to v2, except the linking data is stored at the end, to enable a "don't copy the last object" optimization. The game's code uses the `work_v2` function on v4 objects as a result, and some of my comments may refer to v2, when I really mean v4.  I believe there are no v2 objects in any games.

# Plan
- Create a library for generating obj files in V3/V4 format
  - V4 should match game exactly. Doesn't support code.
  - V3 is our own thing. Must support code.
  
We'll eventually create tools which use the library in V4 mode to generate object files for rebuilding levels and textures.  We may need to wait until more about these formats is understood before trying this.

The compiler will use the library in V3 mode to generate object files for each `gc` (GOAL source code) file.

# CGO files
The only CGO files read are `KERNEL.CGO` and `GAME.CGO`. 
 
 The `KERNEL.CGO` contains the GOAL kernel and some very basic libraries (`gcommon`, `gstring`, `gstate`, ...). I believe that `KERNEL` was always loaded on boot during development, as its required for the Listener to function.  

The `GAME.CGO` file combines the contents of the `ENGINE`, `COMMON` and `ART` CGO files.  `ENGINE` contains the game engine code, `COMMON` contains level-specific code (outside of the game engine) that is always loaded.  If code is used in basically all the levels, it makes sense to put in in `COMMON`, so it doesn't have to be loaded for each currently active level.  The `ART` CGO contains common art/textures/models, like Jak and his animations.  

The `JUNGLE.CGO`, `MAINCAVE.CGO`, `SUNKEN.CGO` file contains some copies of files used in the jungle, cave, LPC levels. Some are a tiny bit different. I believe it is unused. 

The `L1.CGO` file contains basically all the level-specific code/Jak animations and some textures.  It doesn't seem to contain any 3D models.  It's unused, but I'm still interested in understanding its format, as the Jak 1 demos have this file.

The `RACERP.CGO` file contains (I think) everything needed for the Zoomer. Unused. The same data appears in the levels as needed, maybe with some slight differences.

The `VILLAGEP.CGO` file contains common code shared in village levels, which isn't much (oracle, warp gate). Unused. The same data appears in the levels as needed.

The `WATER-AN.CGO` file contains some small code/data for water animations. Unused. The same data appears in the levels as needed.

# CGO/DGO Loading Process
A CGO/DGO file is loaded onto a "heap", which is just a chunk of contiguous memory.  The loading process is designed to be fast, and also able to fill the entire heap, and allow each object to allocate memory after it is loaded.  The process works like this:

1. Two temporary buffers are allocated at the end of the heap. These are sized so that they can fit the largest object file, not including the last object file. 
2. The IOP begins loading, and is permitted to load the first two object files to the two temporary buffers
3. The main CPU waits for the first object file to be loaded.
4. While the second object file being loaded, the first object is "linked". The first step to linking is to copy the object file data from the temporary buffer to the bottom of the heap, kicking out all the other data in the process. The linking data is checked to see if it is in the top of the heap, and is moved there if it isn't already. The run-once initialization code is copied to another temporary allocation on top of the heap and the debug data is copied to the debug heap.
5. Still, while the second object file is being loaded, the linker runs on the first object file.
6. Still, while the second object file is being loaded, the second object's initialization code is run (located in top of the heap). The second object may allocate from this heap, and will get valid memory without creating gaps in the heap.
7. Memory allocated from the top during linking is freed.
8. The IOP is allowed to load into the first buffer again.
9. The main CPU waits for the second object to be loaded, if the IOP hasn't finished yet.
10. This double-buffering pattern continues - while one object is loaded into a buffer, the other one will be copied to the bottom of the heap, linked, and initialized. When the second to last object is loaded, the IOP will wait an extra time until the main CPU has finished linking it until loading the last object (one additional wait) because the last object has a special case.
11. The last object will be loaded directly onto the bottom of the heap, as there may not be enough memory to use the temporary buffers and load the last object. The temporary buffers are freed.
12. If the last object is a v3, its linking data will be moved to the top-level, and the object data will be moved to fill in the gap left behind. If the last object is a v2, the main data will be at the beginning of the object data, so there is an optimization that will avoid copying the object data to save time, if the data is already close to being in the right place.


Generally the last file in a level DGO will be the largest v4 object.  You can only have one file larger than a temporary buffer, and it must come last. The last file also doesn't have to be copied after being loaded into memory if it is a v4.

V3 max size:
 A V3 object is copied all at once with a single `ultimate-memcpy`.  Usually linking gets to run for around 3 to 5% of a total frame. The `ultimate-memcpy` routine does a to/from scratchpad transfer. In practice, mem/spr transfers are around 1800 MB/sec, and the data has to be copied twice, so the effective bandwidth is 900 MB/sec.
 
 `900 MB / second * (0.04 * 0.0167 seconds) = 601 kilobytes`
 
 This estimate is backed up by the the chunk size of the v4 copy routine, which copies one chunk per frame.  It picks 524 kB as the maximum amount that's safe to copy per frame.
 