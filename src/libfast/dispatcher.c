/*
 * Copyright (C) 2007 Martin Willi
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "dispatcher.h"

#include "request.h"
#include "session.h"

#include <fcgiapp.h>
#include <signal.h>
#include <unistd.h>

#include <debug.h>
#include <threading/thread.h>
#include <threading/condvar.h>
#include <threading/mutex.h>
#include <utils/linked_list.h>
#include <utils/hashtable.h>

/** Intervall to check for expired sessions, in seconds */
#define CLEANUP_INTERVAL 30

typedef struct private_dispatcher_t private_dispatcher_t;

/**
 * private data of the task manager
 */
struct private_dispatcher_t {

	/**
	 * public functions
	 */
	dispatcher_t public;

	/**
	 * fcgi socket fd
	 */
	int fd;

	/**
	 * thread list
	 */
	thread_t **threads;

	/**
	 * number of threads in "threads"
	 */
	int thread_count;

	/**
	 * session locking mutex
	 */
	mutex_t *mutex;

	/**
	 * Hahstable with active sessions
	 */
	hashtable_t *sessions;

	/**
	 * session timeout
	 */
	time_t timeout;

	/**
	 * timestamp of last session cleanup round
	 */
	time_t last_cleanup;

	/**
	 * running in debug mode?
	 */
	bool debug;

	/**
	 * List of controllers controller_constructor_t
	 */
	linked_list_t *controllers;

	/**
	 * List of filters filter_constructor_t
	 */
	linked_list_t *filters;

	/**
	 * constructor function to create session context (in controller_entry_t)
	 */
	context_constructor_t context_constructor;

	/**
	 * user param to context constructor
	 */
	void *param;
};

typedef struct {
	/** constructor function */
	controller_constructor_t constructor;
	/** parameter to constructor */
	void *param;
} controller_entry_t;

typedef struct {
	/** constructor function */
	filter_constructor_t constructor;
	/** parameter to constructor */
	void *param;
} filter_entry_t;

typedef struct {
	/** session instance */
	session_t *session;
	/** condvar to wait for session */
	condvar_t *cond;
	/** client host address, to prevent session hijacking */
	char *host;
	/** TRUE if session is in use */
	bool in_use;
	/** last use of the session */
	time_t used;
	/** has the session been closed by the handler? */
	bool closed;
} session_entry_t;

/**
 * create a session and instanciate controllers
 */
static session_t* load_session(private_dispatcher_t *this)
{
	enumerator_t *enumerator;
	controller_entry_t *centry;
	filter_entry_t *fentry;
	session_t *session;
	context_t *context = NULL;
	controller_t *controller;
	filter_t *filter;

	if (this->context_constructor)
	{
		context = this->context_constructor(this->param);
	}
	session = session_create(context);

	enumerator = this->controllers->create_enumerator(this->controllers);
	while (enumerator->enumerate(enumerator, &centry))
	{
		controller = centry->constructor(context, centry->param);
		session->add_controller(session, controller);
	}
	enumerator->destroy(enumerator);

	enumerator = this->filters->create_enumerator(this->filters);
	while (enumerator->enumerate(enumerator, &fentry))
	{
		filter = fentry->constructor(context, fentry->param);
		session->add_filter(session, filter);
	}
	enumerator->destroy(enumerator);

	return session;
}

/**
 * create a new session entry
 */
static session_entry_t *session_entry_create(private_dispatcher_t *this,
											 char *host)
{
	session_entry_t *entry;

	entry = malloc_thing(session_entry_t);
	entry->in_use = FALSE;
	entry->closed = FALSE;
	entry->cond = condvar_create(CONDVAR_TYPE_DEFAULT);
	entry->session = load_session(this);
	entry->used = time_monotonic(NULL);
	entry->host = strdup(host);

	return entry;
}

/**
 * destroy a session
 */
static void session_entry_destroy(session_entry_t *entry)
{
	entry->session->destroy(entry->session);
	entry->cond->destroy(entry->cond);
	free(entry->host);
	free(entry);
}

/**
 * Implementation of dispatcher_t.add_controller.
 */
static void add_controller(private_dispatcher_t *this,
						   controller_constructor_t constructor, void *param)
{
	controller_entry_t *entry = malloc_thing(controller_entry_t);

	entry->constructor = constructor;
	entry->param = param;
	this->controllers->insert_last(this->controllers, entry);
}

/**
 * Implementation of dispatcher_t.add_filter.
 */
static void add_filter(private_dispatcher_t *this,
					   filter_constructor_t constructor, void *param)
{
	filter_entry_t *entry = malloc_thing(filter_entry_t);

	entry->constructor = constructor;
	entry->param = param;
	this->filters->insert_last(this->filters, entry);
}

/**
 * Hashtable hash function
 */
static u_int session_hash(char *sid)
{
	return chunk_hash(chunk_create(sid, strlen(sid)));
}

/**
 * Hashtable equals function
 */
static bool session_equals(char *sid1, char *sid2)
{
	return streq(sid1, sid2);
}

/**
 * Cleanup unused sessions
 */
static void cleanup_sessions(private_dispatcher_t *this, time_t now)
{
	if (this->last_cleanup < now - CLEANUP_INTERVAL)
	{
		char *sid;
		session_entry_t *entry;
		enumerator_t *enumerator;
		linked_list_t *remove;

		this->last_cleanup = now;
		remove = linked_list_create();
		enumerator = this->sessions->create_enumerator(this->sessions);
		while (enumerator->enumerate(enumerator, &sid, &entry))
		{
			/* check all sessions for timeout or close flag */
			if (!entry->in_use &&
				(entry->used < now - this->timeout || entry->closed))
			{
				remove->insert_last(remove, sid);
			}
		}
		enumerator->destroy(enumerator);

		while (remove->remove_last(remove, (void**)&sid) == SUCCESS)
		{
			entry = this->sessions->remove(this->sessions, sid);
			if (entry)
			{
				session_entry_destroy(entry);
			}
		}
		remove->destroy(remove);
	}
}

/**
 * Actual dispatching code
 */
static void dispatch(private_dispatcher_t *this)
{
	thread_cancelability(FALSE);

	while (TRUE)
	{
		request_t *request;
		session_entry_t *found = NULL;
		time_t now;
		char *sid;

		thread_cancelability(TRUE);
		request = request_create(this->fd, this->debug);
		thread_cancelability(FALSE);

		if (request == NULL)
		{
			continue;
		}
		now = time_monotonic(NULL);
		sid = request->get_cookie(request, "SID");

		this->mutex->lock(this->mutex);
		if (sid)
		{
			found = this->sessions->get(this->sessions, sid);
		}
		if (found && !streq(found->host, request->get_host(request)))
		{
			found = NULL;
		}
		if (found)
		{
			/* wait until session is unused */
			while (found->in_use)
			{
				found->cond->wait(found->cond, this->mutex);
			}
		}
		else
		{	/* create a new session if not found */
			found = session_entry_create(this, request->get_host(request));
			sid = found->session->get_sid(found->session);
			this->sessions->put(this->sessions, sid, found);
		}
		found->in_use = TRUE;
		this->mutex->unlock(this->mutex);

		/* start processing */
		found->session->process(found->session, request);
		found->used = time_monotonic(NULL);

		/* release session */
		this->mutex->lock(this->mutex);
		found->in_use = FALSE;
		found->closed = request->session_closed(request);
		found->cond->signal(found->cond);
		cleanup_sessions(this, now);
		this->mutex->unlock(this->mutex);

		request->destroy(request);
	}
}

/**
 * Implementation of dispatcher_t.run.
 */
static void run(private_dispatcher_t *this, int threads)
{
	this->thread_count = threads;
	this->threads = malloc(sizeof(thread_t*) * threads);
	while (threads)
	{
		this->threads[threads - 1] = thread_create((thread_main_t)dispatch,
												   this);
		if (this->threads[threads - 1])
		{
			threads--;
		}
	}
}

/**
 * Implementation of dispatcher_t.waitsignal.
 */
static void waitsignal(private_dispatcher_t *this)
{
	sigset_t set;
	int sig;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGHUP);
	sigprocmask(SIG_BLOCK, &set, NULL);
	sigwait(&set, &sig);
}

/**
 * Implementation of dispatcher_t.destroy
 */
static void destroy(private_dispatcher_t *this)
{
	char *sid;
	session_entry_t *entry;
	enumerator_t *enumerator;

	FCGX_ShutdownPending();
	while (this->thread_count--)
	{
		thread_t *thread = this->threads[this->thread_count];
		thread->cancel(thread);
		thread->join(thread);
	}
	enumerator = this->sessions->create_enumerator(this->sessions);
	while (enumerator->enumerate(enumerator, &sid, &entry))
	{
		session_entry_destroy(entry);
	}
	enumerator->destroy(enumerator);
	this->sessions->destroy(this->sessions);
	this->controllers->destroy_function(this->controllers, free);
	this->filters->destroy_function(this->filters, free);
	this->mutex->destroy(this->mutex);
	free(this->threads);
	free(this);
}

/*
 * see header file
 */
dispatcher_t *dispatcher_create(char *socket, bool debug, int timeout,
								context_constructor_t constructor, void *param)
{
	private_dispatcher_t *this = malloc_thing(private_dispatcher_t);

	this->public.add_controller = (void(*)(dispatcher_t*, controller_constructor_t, void*))add_controller;
	this->public.add_filter = (void(*)(dispatcher_t*,filter_constructor_t constructor, void *param))add_filter;
	this->public.run = (void(*)(dispatcher_t*, int threads))run;
	this->public.waitsignal = (void(*)(dispatcher_t*))waitsignal;
	this->public.destroy = (void(*)(dispatcher_t*))destroy;

	this->sessions = hashtable_create((void*)session_hash,
									  (void*)session_equals, 4096);
	this->controllers = linked_list_create();
	this->filters = linked_list_create();
	this->context_constructor = constructor;
	this->mutex = mutex_create(MUTEX_TYPE_DEFAULT);
	this->param = param;
	this->fd = 0;
	this->timeout = timeout;
	this->last_cleanup = time_monotonic(NULL);
	this->debug = debug;
	this->threads = NULL;

	FCGX_Init();

	if (socket)
	{
		unlink(socket);
		this->fd = FCGX_OpenSocket(socket, 10);
	}
	return &this->public;
}

