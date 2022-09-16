# The Effect of Cache Hotness on Task Scheduling

## Introduction

When designing a task scheduler (or choosing scheduling parameters), one needs to consider the impact of "cache
hotness". However, I've never seen a source quantify how much of an impact cache hotness actually does. In this blog
post I first introduce the concept of cache hotness. Thereafter, I explain what kind of things affects it. Then, I show
my own test setup. Lastly, I share measurement result I got and discuss what these mean.

## What Is Cache Hotness

"Cache Hotness" is not a well-defined term. In lack of a better vocabulary, I will use it all the same. By "cache
hotness" I am refering to how "hot" or "cold" the cache is. Unfortunately, "hot cache" and "cold cache", respectively,
are not well-defined either. Instead, we need to do a few definitions.

Let's define a task that has a completely cold cache as a task that has all of it's data in memory that is as remote as
possible. Typically, this would mean that a task with a completely cold cache has all of it's data in the main memory.
Main memory is often known as RAM memory.

Furthermore, let's define a task that has a fully hot cache as a task that has all of it's data in memory that is as
close to the processing unit as possible. Typically, this would mean that all of the L1 data and/or L1 instruction
memory, L2 memory and so forth would be populated with as much data of the task as possible. Note that if the data of
the task is less than the L1 data memory, then the L1 data memory can be partially populated with data from unrelated
tasks. Conversely, if the L1 data memory is smaller, then only data of the task and relevant processes can reside in the
L1 data cache.

To reiterate, let's assume we have two tasks, task A and task B. As task A runs and accesses it's memory, the cache
hotness of the task increases. Conversely, when task A is switched out for task B and the cache is being populated with
memory of task B, the cache hotness of task A decreases.

## What Impacts Cache Hotness

The way the memory of the modern processor works is rather involved. For a comprehensive review on the subject, the
interested reader is recommended to read the famous paper by Ulrich Drepper [What Every Programmer Should Know About
Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf). In this blog post we will keep things somewhat
simpler.

Obviously, the most important factor that affects cache hotness is how much memory the program utilizes compared to how
much cache memory we have. Furthermore, we need to care about how much memory a task accesses before being switched out.
Moreover, it depends on what type of accesses the task does. If the task always accesses the same subset of it's memory,
that subset can be kept in fast cache memory.

## Layman Explanation Of The Test Setup

Before presenting the technical details of the test setup, let's give an analogy of the test setup that should be
possible for the layman to understand. Let's assume that we have two researchers, researcher A and researcher B, sharing
a lab environment. The lab environment can be used by one researcher at a time.

As researcher A is switched out for researcher B, it takes some time for researcher B to switch out the equipment used
by researcher A for the equipment researcher B needs. Conversely, when researcher B is switched out for researcher A,
researcher A will use some time to switch out the equipment.

All else being equal, we can simply deduce that the most efficient setting would be to have one researcher, either
researcher A or researcher B (the order does not matter), complete their research switching who is allowed to use the
lab. However, this would not be fair nor desirable, if we want both to make constant progress.

Therefore, we would like to find a balance between switching too often and too seldom. If we switch too seldom, it's
unfair and there's a substantial delay in the progress of the researcher who's switched out. On the other hand, if we
switch too often, a substantial amount of time is spend of moving around lab equipment rather than doing actual
research.

With the layman explanation done, let's move forward to the technical explanation and details.

## Test Setup

In our test setup, we have two tasks. They are Linux processes. Both are scheduled with `SCHED_FIFO` and are set to use
the same fifo priority. Furthermore, they are pinned to a single CPU core. To switch between the tasks, we use
co-operative scheduling. One task runs for a predefined amount of cycles and calls `sched_yield()` in order for the
Linux scheduler to switch to the other task. At this point the other task takes over, runs for some time and calls
`sched_yield()`. Ultimately, after running a predefined amount of `sched_yields()`, the loop ends and runtimes are
collected.

The basic idea is to run every measurement twice, with a slight adjustment. First, we run the tasks "concurrently",
meaning that both will do a substantial amount of memory accesses between calling `sched_yield()`. Thereafter, we run
the tasks "in sequence", i.e. one of the tasks will operate as previously while the other will immediately call
`sched_yield()`. The sequential run gives us a baseline to compare the concurrent run against.

Note that calling `sched_yield()` has some overhead (albeit small). Therefore, we need to make the exact same amount of
calls to `sched_yield()` in both measurements. Otherwise we would be also measuring the overhead of `sched_yield()`.

During program initialization, the tasks will reserve a predefined amount of memory, for example, 4 megabytes. This
memory is immediately faulted on so that page faults would not distort the measurements. While the actual measuring is
running, the tasks will access the memory in a "uniform" fashion. I.e. each task will access the memory in order, and
switch back to the beginning after reaching the end.

Due to how the modern processor works, memory accesses will by default be done once every cache line size. For example,
if the cache line size is 64 bytes, memory accesses will be done at 0 offset, then 64 offset, then 128 offset etc.

The tasks will iterate a predefined amount of times over all of the reserved memory and then call `sched_yield()`.

We will do measurements across a few different parameters. The first parameter is the total amount of memory a task
accesses, denoted by `memory_total`. The second parameter is the amount of yields, denoted by `yield_count`. The third
parameter is the amount of iterations during every scheduled slot, denoted by `iterations_per_yield`. The fourth, and
final, parameter is the amount of accesses per cache line, denoted by `access_per_cache_line`.

In summary, we measure over the following parameters:

1. `memory_total` - total amount of memory
1. `yield_count` - amount of yields
1. `iterations_per_yield` - iterations during each scheduled slot
1. `access_per_cache_line` - accesses per cache line

The amount of memory accesses during a single measurement can be calculated with the following formula:

    access_total = memory_total / cache_line_size * access_per_cache_line * iterations_per_yield * yield_count

As we are constrained by memory latency, increasing `access_per_cache_line` has insignificant impact. However, if we
increase it enough, we should eventually become constrained by raw CPU power.

We will do measurements on 3 different computers. The first computer is a desktop computer with a rather powerful
processor, namely AMD Ryzen 9 5950X. The second is a laptop, with TODO. The third is a rapberry pi, with TODO.

CPU frequency scaling is turned off on all computers. The CPU core that is used for the measurement is isolated with
[isolcpus](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html?highlight=isolcpu). All computers
are running Ubuntu 22.04.

The source code for the measurement program can be downloaded at
https://github.com/keelefi/blog/releases/download/cache-hotness-v1.3/cache-hotness-1.3.tar.gz

## Results

## Discussion

## Future Work

Measure the effect of cache associativity.

Measure tasks on various cores.

Use non-uniform memory accesses.

Increase amount of instructions.

