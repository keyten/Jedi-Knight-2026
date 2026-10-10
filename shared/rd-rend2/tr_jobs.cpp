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
// tr_jobs.cpp -- worker threads for load-time work

/*
Two kinds of work run on a small pool of worker threads:

- R_ParallelFor splits [0, count) into chunks of `grain` and runs them on the
  calling (main) thread and on every idle worker. It returns when every chunk
  is done. A worker busy with an asynchronous task joins late or not at all;
  the main thread never waits for it to become idle.
- R_JobSubmit queues one asynchronous task (prefetch decodes, tr_prefetch.cpp).
  R_JobWait returns when it is done; while waiting, the main thread runs
  queued tasks itself.

Workers only run the function they are given: no GL, filesystem, zone memory
(Z_Malloc / R_Malloc), cvars or ri.Printf. The caller allocates every buffer
beforehand. A kernel must give the same bytes however it is split, so results
do not depend on r_loadThreads.

r_loadThreads: -1 = number of logical processors - 1, at most 8; 0 = no
workers (ParallelFor runs serially, R_JobSubmit runs the task at once);
N = N workers (max 16). The pool follows the cvar at the next ParallelFor
while no task is queued, and is joined in RE_Shutdown.
*/

#include "tr_local.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

struct R_Job {
	void (*fn)( void *user );
	void *user;
	bool done;
};

namespace {
struct ParallelJob {
	R_ParallelForFn fn;
	void *user;
	int count, grain;
	std::atomic<int> next{ 0 };
	int active = 0;		// workers inside RunChunks, guarded by the pool mutex
};

struct JobPool {
	std::vector<std::thread> workers;
	std::mutex mutex;
	std::condition_variable wake;
	std::condition_variable done;
	bool quit = false;
	ParallelJob *current = nullptr;
	std::deque<R_Job *> tasks;
	int outstanding = 0;	// submitted, not yet waited for

	// exit without RE_Shutdown (fatal error): the OS has already stopped the
	// workers, and destroying a joinable std::thread would call terminate()
	~JobPool() {
		for (std::thread &worker : workers)
			if (worker.joinable())
				worker.detach();
	}
};

JobPool pool;

void RunChunks( ParallelJob *job ) {
	for ( ;; ) {
		const int begin = job->next.fetch_add(job->grain);
		if (begin >= job->count)
			return;
		job->fn(job->user, begin, std::min(begin + job->grain, job->count));
	}
}

bool HasChunks( const ParallelJob *job ) {
	return job && job->next.load() < job->count;
}

// runs one queued task; called with the lock held, returns with it held
void RunTask( std::unique_lock<std::mutex> &lock ) {
	R_Job *task = pool.tasks.front();
	pool.tasks.pop_front();
	lock.unlock();
	task->fn(task->user);
	lock.lock();
	task->done = true;
	pool.done.notify_all();
}

void WorkerMain() {
	std::unique_lock<std::mutex> lock(pool.mutex);
	for ( ;; ) {
		pool.wake.wait(lock, [] { return pool.quit || HasChunks(pool.current) || !pool.tasks.empty(); });
		if (pool.quit)
			return;
		if (HasChunks(pool.current)) {
			ParallelJob *job = pool.current;
			++job->active;
			lock.unlock();
			RunChunks(job);
			lock.lock();
			if (--job->active == 0)
				pool.done.notify_all();
			continue;
		}
		RunTask(lock);
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

void StopWorkers() {
	{
		std::lock_guard<std::mutex> lock(pool.mutex);
		pool.quit = true;
	}
	pool.wake.notify_all();
	for (std::thread &worker : pool.workers)
		worker.join();
	pool.workers.clear();
	pool.quit = false;
}

void FollowCvar() {
	const int wanted = WantedWorkers();
	if (wanted == (int)pool.workers.size() || pool.outstanding)
		return;
	StopWorkers();
	for (int i = 0; i < wanted; ++i)
		pool.workers.emplace_back(WorkerMain);
}
}

void R_JobsShutdown( void ) {
	// every submitted task must have been waited for (R_PrefetchFlush)
	{
		std::unique_lock<std::mutex> lock(pool.mutex);
		while (!pool.tasks.empty())
			RunTask(lock);
	}
	StopWorkers();
}

int R_JobWorkers( void ) {
	if (pool.workers.empty())
		FollowCvar();
	return (int)pool.workers.size();
}

void R_ParallelFor( int count, int grain, R_ParallelForFn fn, void *user ) {
	if (count <= 0)
		return;
	grain = std::max(grain, 1);
	FollowCvar();

	if (pool.workers.empty() || count <= grain) {
		fn(user, 0, count);
		return;
	}

	ParallelJob job;
	job.fn = fn;
	job.user = user;
	job.count = count;
	job.grain = grain;
	{
		std::lock_guard<std::mutex> lock(pool.mutex);
		pool.current = &job;
	}
	pool.wake.notify_all();

	RunChunks(&job);

	// no worker may pick the job up any more; wait for those inside it
	std::unique_lock<std::mutex> lock(pool.mutex);
	pool.current = nullptr;
	pool.done.wait(lock, [&] { return job.active == 0; });
}

R_Job *R_JobSubmit( void (*fn)( void *user ), void *user ) {
	R_Job *task = new R_Job{ fn, user, false };
	if (pool.workers.empty()) {
		fn(user);
		task->done = true;
		++pool.outstanding;
		return task;
	}
	{
		std::lock_guard<std::mutex> lock(pool.mutex);
		pool.tasks.push_back(task);
		++pool.outstanding;
	}
	pool.wake.notify_one();
	return task;
}

qboolean R_JobDone( R_Job *task ) {
	std::lock_guard<std::mutex> lock(pool.mutex);
	return (qboolean)task->done;
}

void R_JobWait( R_Job *task ) {
	std::unique_lock<std::mutex> lock(pool.mutex);
	while (!task->done) {
		if (!pool.tasks.empty())
			RunTask(lock);	// help instead of idling (may be this very task)
		else
			pool.done.wait(lock);
	}
	--pool.outstanding;
	lock.unlock();
	delete task;
}
