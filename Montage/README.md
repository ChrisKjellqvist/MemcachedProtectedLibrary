This folder contains infrastructures for fast recoverable data structures. For
pseudocode of (the original) epoch system, see:

https://docs.google.com/document/d/1J_hAxgGEVqVhe89moDAQskpgZQAUYICUoWUUBOYqK1U/edit


## Environment variables and values
usage: add argument:
```
-d<env>=<val>
```
to command line.

* `Persist`: specify persist strategies
    * `DirWB`: directly write back every update to persistent blocks, and only issue an `sfence` on epoch advance
    * `PerLine`: keep to-be-persisted records of an epoch on a per-cache-line basis and flush them together
    * `DelayedWB` (default): keep to-be-persisted records of an epoch on a per-cache-line basis in a fixed-size buffer and flush them all when it's full
    * `No`: No persistence operations. NOTE: epoch advancing and all epoch-related persistency will be shut down. Overrides `EpochAdvance`, `EpochFreq` and `HelpFreq` environments.
* `TransCounter`: specify the type of active (data structure and bookkeeping) transaction counter
    * `Atomic`: a global atomic int counter for each epoch. lock-prefixed instruction on each update.
    * `NoFence`: per-thread true-false indicator updated with release consistency without mfences. (potentially unsafe)
    * `FenceBegin`: based on `NoFence`, adding an mfence at the _beginning_ of each operation after indicator modification.
    * `FenceEnd`: based on `NoFence`, adding an mfence at the _end_ of each operation after indicator modification. (potentially unsafe, not sure if interesting or not)
* `EpochAdvance`: specify epoch advance strategy
    * `Dedicated`  (default): have a dedicated thread advances the global epoch once in a while and does write-backs and memory reclamations
    * `SingleThread`: only a single thread (thread 0) advances the global epoch, while other threads only perform helping. Every thread has a thread-local operation counter.
    * `Global`: all threads advance a global operation counter. Any thread can advance the global epoch.
* `EpochFreq`: specify epoch advance frequency: epoch will be advanced every 2^value operations performed globally.
    * value: integer between 2 and 63
* `HelpFreq`: specify helping frequency: epoch will be advanced every 2^value operations performed globally.
    * value: integer between 2 and 63

TODO:

Design:
    [x] Epoch system
    [x] API

Implementation:
    [x] Epoch system
        [x] Implementation
        [x] Tests
    [x] Modify Ralloc
    [x] Example Data Structures
    [ ] Verification

Optimizations:
    [x] per-thread to-be-persist and to-be-freed containers
