/*
===========================================================================
Copyright (C) 2026 OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/
// tr_jobs.cpp -- worker threads for data-parallel load-time kernels

/*
R_ParallelFor splits [0, count) into chunks of `grain` and runs them on the
calling (main) thread and a small pool of workers. It returns when every chunk
is done. Workers only run the function they are given: no GL, filesystem,
zone memory (Z_Malloc / R_Malloc), cvars or ri.Printf in a chunk. The
caller allocates every buffer beforehand. A kernel must give the same bytes
however it is split, so results do not depend on r_loadThreads.

r_loadThreads: -1 = number of logical processors - 1, at most 8; 0 = every
kernel runs serially on the main thread, as before; N = N workers (max 16).
The pool follows the cvar at the next call and is joined in RE_Shutdown.
*/

#include "tr_local.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {
struct JobPool {
	std::vector<std::thread> workers;
	std::mutex mutex;
	std::condition_variable wake;
	std::condition_variable done;
	unsigned generation = 0;
	int running = 0;		// workers still inside the current generation
	bool quit = false;

	R_ParallelForFn fn = nullptr;
	void *user = nullptr;
	int count = 0;
	int grain = 1;
	std::atomic<int> next{ 0 };

	// exit without RE_Shutdown (fatal error): the OS has already stopped the
	// workers, and destroying a joinable std::thread would call terminate()
	~JobPool() {
		for (std::thread &worker : workers)
			if (worker.joinable())
				worker.detach();
	}
};

JobPool pool;

void RunChunks() {
	for ( ;; ) {
		const int begin = pool.next.fetch_add(pool.grain);
		if (begin >= pool.count)
			return;
		pool.fn(pool.user, begin, std::min(begin + pool.grain, pool.count));
	}
}

void WorkerMain() {
	unsigned seen = 0;
	for ( ;; ) {
		{
			std::unique_lock<std::mutex> lock(pool.mutex);
			pool.wake.wait(lock, [&] { return pool.quit || pool.generation != seen; });
			if (pool.quit)
				return;
			seen = pool.generation;
		}
		RunChunks();
		{
			std::lock_guard<std::mutex> lock(pool.mutex);
			if (--pool.running == 0)
				pool.done.notify_one();
		}
	}
}

cvar_t *LoadThreadsCvar() {
#ifdef REND2_SP
	static cvar_t *threads = ri.Cvar_Get("r_loadThreads", "-1", CVAR_ARCHIVE);
#else
	static cvar_t *threads = ri.Cvar_Get("r_loadThreads", "-1", CVAR_ARCHIVE,
		"Worker threads for load-time texture work: -1 = automatic, 0 = none");
#endif
	return threads;
}

int WantedWorkers() {
	const cvar_t *threads = LoadThreadsCvar();
	const int value = threads ? threads->integer : 0;
	if (value == 0)
		return 0;
	if (value > 0)
		return std::min(value, 16);
	const int logical = (int)std::thread::hardware_concurrency();
	return Com_Clampi(0, 8, logical - 1);
}
}

void R_JobsShutdown( void ) {
	{
		std::lock_guard<std::mutex> lock(pool.mutex);
		pool.quit = true;
	}
	pool.wake.notify_all();
	for (std::thread &worker : pool.workers)
		worker.join();
	pool.workers.clear();
	pool.quit = false;
	pool.running = 0;
}

int R_JobWorkers( void ) {
	return (int)pool.workers.size();
}

void R_ParallelFor( int count, int grain, R_ParallelForFn fn, void *user ) {
	if (count <= 0)
		return;
	grain = std::max(grain, 1);

	const int wanted = WantedWorkers();
	if (wanted != (int)pool.workers.size()) {
		R_JobsShutdown();
		for (int i = 0; i < wanted; ++i)
			pool.workers.emplace_back(WorkerMain);
	}

	if (pool.workers.empty() || count <= grain) {
		fn(user, 0, count);
		return;
	}

	{
		std::lock_guard<std::mutex> lock(pool.mutex);
		pool.fn = fn;
		pool.user = user;
		pool.count = count;
		pool.grain = grain;
		pool.next.store(0);
		pool.running = (int)pool.workers.size();
		++pool.generation;
	}
	pool.wake.notify_all();

	RunChunks();

	std::unique_lock<std::mutex> lock(pool.mutex);
	pool.done.wait(lock, [] { return pool.running == 0; });
	pool.fn = nullptr;
	pool.user = nullptr;
}
