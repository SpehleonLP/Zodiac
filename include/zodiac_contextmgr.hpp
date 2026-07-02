#ifndef ZODIAC_CONTEXTMGR_HPP
#define ZODIAC_CONTEXTMGR_HPP

// -----------------------------------------------------------------------------
// Optional CContextMgr save/load glue.
//
// This block reaches into `protected` CContextMgr internals (m_getTimeFunc,
// m_threads, m_currentThread) and the (only forward-declared in stock)
// CContextMgr::SContextInfo. The stock 2.38.0 contextmgr add-on does NOT expose
// these, so this file is compiled ONLY when the parent engine ships the patched
// contextmgr and defines ZODIAC_HAVE_PATCHED_CONTEXTMGR. The stock standalone
// build never defines it, so this header expands to nothing and imposes no
// compat burden. Multi-context serialization stays covered by the raw
// SaveContext/LoadContext path (see tests/test_multi_context, test_context_roots).
// -----------------------------------------------------------------------------

#ifdef HAVE_ZODIAC
#ifdef ZODIAC_HAVE_PATCHED_CONTEXTMGR

#ifndef ZODIAC_H
#include "zodiac.h"
#endif

#include "add_on/contextmgr/contextmgr.h"

#include <algorithm>
#include <cstdint>

namespace Zodiac
{
	inline void ZodiacSaveContextManager(zIZodiacWriter * writer, AS_NAMESPACE_QUALIFIER CContextMgr const* mgr, int&)
	{
		int64_t time{};
		if(mgr->m_getTimeFunc)
		{
			time = (mgr->m_getTimeFunc)();
		}

		auto file = writer->GetFile();

		uint32_t noThreads = mgr->m_threads.size();
		uint32_t curThread = mgr->m_currentThread;

		file->Write(&noThreads);
		file->Write(&curThread);

		for(auto p : mgr->m_threads)
		{
			int sleepUntil = std::max<int64_t>((int64_t)p->sleepUntil - time, 0);
			uint32_t size = p->coRoutines.size();

			file->Write(&sleepUntil);
			file->Write(&p->currentCoRoutine);
			file->Write(&size);

			int id = writer->SaveContext(p->keepCtxAfterExecution);

			file->Write(&id);

			for(auto ctx : p->coRoutines)
			{
				id = writer->SaveContext(ctx);
				file->Write(&id);
			}
		}
	}

	inline void ZodiacLoadContextManager(zIZodiacReader * reader, AS_NAMESPACE_QUALIFIER CContextMgr* mgr, int&, bool)
	{
		uint32_t time{};
		if(mgr->m_getTimeFunc)
		{
			time = (mgr->m_getTimeFunc)();
		}

		auto file = reader->GetFile();

		uint32_t noThreads{};
		uint32_t curThread{};

		file->Read(&noThreads);
		file->Read(&curThread);

		mgr->m_currentThread = curThread;
		mgr->m_threads.resize(noThreads, nullptr);

		for(auto & p : mgr->m_threads)
		{
			p = new CContextMgr::SContextInfo();
			int sleepUntil{};
			uint32_t size{};
			int thread_id{};

			file->Read(&sleepUntil);
			file->Read(&p->currentCoRoutine);
			file->Read(&size);
			file->Read(&thread_id);

			p->sleepUntil = (int64_t)sleepUntil + time;
			p->keepCtxAfterExecution = reader->LoadContext(thread_id);
			p->coRoutines.resize(size, nullptr);

			for(auto & ctx : p->coRoutines)
			{
				file->Read(&thread_id);
				ctx = reader->LoadContext(thread_id);
			}

			if(p->keepCtxAfterExecution)
				p->keepCtxAfterExecution->Release();
		}
	}
}

#endif // ZODIAC_HAVE_PATCHED_CONTEXTMGR
#endif // HAVE_ZODIAC

#endif // ZODIAC_CONTEXTMGR_HPP
