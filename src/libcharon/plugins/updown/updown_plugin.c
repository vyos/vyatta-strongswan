/*
 * Copyright (C) 2008 Martin Willi
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

#include "updown_plugin.h"
#include "updown_listener.h"

#include <daemon.h>

typedef struct private_updown_plugin_t private_updown_plugin_t;

/**
 * private data of updown plugin
 */
struct private_updown_plugin_t {

	/**
	 * implements plugin interface
	 */
	updown_plugin_t public;

	/**
	 * Listener interface, listens to CHILD_SA state changes
	 */
	updown_listener_t *listener;
};

METHOD(plugin_t, get_name, char*,
	private_updown_plugin_t *this)
{
	return "updown";
}

METHOD(plugin_t, destroy, void,
	private_updown_plugin_t *this)
{
	charon->bus->remove_listener(charon->bus, &this->listener->listener);
	this->listener->destroy(this->listener);
	free(this);
}

/*
 * see header file
 */
plugin_t *updown_plugin_create()
{
	private_updown_plugin_t *this;

	INIT(this,
		.public = {
			.plugin = {
				.get_name = _get_name,
				.reload = (void*)return_false,
				.destroy = _destroy,
			},
		},
		.listener = updown_listener_create(),
	);

	charon->bus->add_listener(charon->bus, &this->listener->listener);

	return &this->public.plugin;
}

