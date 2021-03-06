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

#include "crypto_factory.h"

#include <debug.h>
#include <threading/rwlock.h>
#include <utils/linked_list.h>
#include <crypto/crypto_tester.h>

const char *default_plugin_name = "default";

typedef struct entry_t entry_t;

struct entry_t {
	/**
	 * algorithm
	 */
	u_int algo;

	/**
	 * plugin that registered this algorithm
	 */
	const char *plugin_name;

	/**
	 * benchmarked speed
	 */
	u_int speed;

	/**
	 * constructor
	 */
	union {
		crypter_constructor_t create_crypter;
		aead_constructor_t create_aead;
		signer_constructor_t create_signer;
		hasher_constructor_t create_hasher;
		prf_constructor_t create_prf;
		rng_constructor_t create_rng;
		dh_constructor_t create_dh;
		void *create;
	};
};

typedef struct private_crypto_factory_t private_crypto_factory_t;

/**
 * private data of crypto_factory
 */
struct private_crypto_factory_t {

	/**
	 * public functions
	 */
	crypto_factory_t public;

	/**
	 * registered crypters, as entry_t
	 */
	linked_list_t *crypters;

	/**
	 * registered aead transforms, as entry_t
	 */
	linked_list_t *aeads;

	/**
	 * registered signers, as entry_t
	 */
	linked_list_t *signers;

	/**
	 * registered hashers, as entry_t
	 */
	linked_list_t *hashers;

	/**
	 * registered prfs, as entry_t
	 */
	linked_list_t *prfs;

	/**
	 * registered rngs, as entry_t
	 */
	linked_list_t *rngs;

	/**
	 * registered diffie hellman, as entry_t
	 */
	linked_list_t *dhs;

	/**
	 * test manager to test crypto algorithms
	 */
	crypto_tester_t *tester;

	/**
	 * whether to test algorithms during registration
	 */
	bool test_on_add;

	/**
	 * whether to test algorithms on each crypto primitive construction
	 */
	bool test_on_create;

	/**
	 * run algorithm benchmark during registration
	 */
	bool bench;

	/**
	 * rwlock to lock access to modules
	 */
	rwlock_t *lock;
};

METHOD(crypto_factory_t, create_crypter, crypter_t*,
	private_crypto_factory_t *this, encryption_algorithm_t algo,
	size_t key_size)
{
	enumerator_t *enumerator;
	entry_t *entry;
	crypter_t *crypter = NULL;

	this->lock->read_lock(this->lock);
	enumerator = this->crypters->create_enumerator(this->crypters);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->algo == algo)
		{
			if (this->test_on_create &&
				!this->tester->test_crypter(this->tester, algo, key_size,
											entry->create_crypter, NULL,
											default_plugin_name))
			{
				continue;
			}
			crypter = entry->create_crypter(algo, key_size);
			if (crypter)
			{
				break;
			}
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
	return crypter;
}

METHOD(crypto_factory_t, create_aead, aead_t*,
	private_crypto_factory_t *this, encryption_algorithm_t algo,
	size_t key_size)
{
	enumerator_t *enumerator;
	entry_t *entry;
	aead_t *aead = NULL;

	this->lock->read_lock(this->lock);
	enumerator = this->aeads->create_enumerator(this->aeads);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->algo == algo)
		{
			if (this->test_on_create &&
				!this->tester->test_aead(this->tester, algo, key_size,
										 entry->create_aead, NULL,
										 default_plugin_name))
			{
				continue;
			}
			aead = entry->create_aead(algo, key_size);
			if (aead)
			{
				break;
			}
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
	return aead;
}

METHOD(crypto_factory_t, create_signer, signer_t*,
	private_crypto_factory_t *this, integrity_algorithm_t algo)
{
	enumerator_t *enumerator;
	entry_t *entry;
	signer_t *signer = NULL;

	this->lock->read_lock(this->lock);
	enumerator = this->signers->create_enumerator(this->signers);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->algo == algo)
		{
			if (this->test_on_create &&
				!this->tester->test_signer(this->tester, algo,
										   entry->create_signer, NULL,
										   default_plugin_name))
			{
				continue;
			}
			signer = entry->create_signer(algo);
			if (signer)
			{
				break;
			}
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);

	return signer;
}

METHOD(crypto_factory_t, create_hasher, hasher_t*,
	private_crypto_factory_t *this, hash_algorithm_t algo)
{
	enumerator_t *enumerator;
	entry_t *entry;
	hasher_t *hasher = NULL;

	this->lock->read_lock(this->lock);
	enumerator = this->hashers->create_enumerator(this->hashers);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (algo == HASH_PREFERRED || entry->algo == algo)
		{
			if (this->test_on_create && algo != HASH_PREFERRED &&
				!this->tester->test_hasher(this->tester, algo,
										   entry->create_hasher, NULL,
										   default_plugin_name))
			{
				continue;
			}
			hasher = entry->create_hasher(entry->algo);
			if (hasher)
			{
				break;
			}
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
	return hasher;
}

METHOD(crypto_factory_t, create_prf, prf_t*,
	private_crypto_factory_t *this, pseudo_random_function_t algo)
{
	enumerator_t *enumerator;
	entry_t *entry;
	prf_t *prf = NULL;

	this->lock->read_lock(this->lock);
	enumerator = this->prfs->create_enumerator(this->prfs);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->algo == algo)
		{
			if (this->test_on_create &&
				!this->tester->test_prf(this->tester, algo,
										entry->create_prf, NULL,
										default_plugin_name))
			{
				continue;
			}
			prf = entry->create_prf(algo);
			if (prf)
			{
				break;
			}
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
	return prf;
}

METHOD(crypto_factory_t, create_rng, rng_t*,
	private_crypto_factory_t *this, rng_quality_t quality)
{
	enumerator_t *enumerator;
	entry_t *entry;
	u_int diff = ~0;
	rng_constructor_t constr = NULL;

	this->lock->read_lock(this->lock);
	enumerator = this->rngs->create_enumerator(this->rngs);
	while (enumerator->enumerate(enumerator, &entry))
	{	/* find the best matching quality, but at least as good as requested */
		if (entry->algo >= quality && diff > entry->algo - quality)
		{
			if (this->test_on_create &&
				!this->tester->test_rng(this->tester, quality,
										entry->create_rng, NULL,
										default_plugin_name))
			{
				continue;
			}
			diff = entry->algo - quality;
			constr = entry->create_rng;
			if (diff == 0)
			{	/* perfect match, won't get better */
				break;
			}
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
	if (constr)
	{
		return constr(quality);
	}
	return NULL;
}

METHOD(crypto_factory_t, create_dh, diffie_hellman_t*,
	private_crypto_factory_t *this, diffie_hellman_group_t group, ...)
{
	enumerator_t *enumerator;
	entry_t *entry;
	diffie_hellman_t *diffie_hellman = NULL;

	this->lock->read_lock(this->lock);
	enumerator = this->dhs->create_enumerator(this->dhs);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->algo == group)
		{
			if (group == MODP_CUSTOM)
			{
				va_list args;
				chunk_t g, p;

				va_start(args, group);
				g = va_arg(args, chunk_t);
				p = va_arg(args, chunk_t);
				va_end(args);
				diffie_hellman = entry->create_dh(MODP_CUSTOM, g, p);
			}
			else
			{
				diffie_hellman = entry->create_dh(group);
			}
			if (diffie_hellman)
			{
				break;
			}
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
	return diffie_hellman;
}

/**
 * Insert an algorithm entry to a list
 */
static void add_entry(private_crypto_factory_t *this, linked_list_t *list,
					  int algo, const char *plugin_name,
					  u_int speed, void *create)
{
	entry_t *entry, *current;
	linked_list_t *tmp;
	bool inserted = FALSE;

	INIT(entry,
		.algo = algo,
		.plugin_name = plugin_name,
		.speed = speed,
	);
	entry->create = create;

	this->lock->write_lock(this->lock);
	if (speed)
	{	/* insert sorted by speed using a temporary list */
		tmp = linked_list_create();
		while (list->remove_first(list, (void**)&current) == SUCCESS)
		{
			tmp->insert_last(tmp, current);
		}
		while (tmp->remove_first(tmp, (void**)&current) == SUCCESS)
		{
			if (!inserted &&
				current->algo == algo &&
				current->speed < speed)
			{
				list->insert_last(list, entry);
				inserted = TRUE;
			}
			list->insert_last(list, current);
		}
		tmp->destroy(tmp);
	}
	if (!inserted)
	{
		list->insert_last(list, entry);
	}
	this->lock->unlock(this->lock);
}

METHOD(crypto_factory_t, add_crypter, void,
	private_crypto_factory_t *this,	encryption_algorithm_t algo,
	const char *plugin_name, crypter_constructor_t create)
{
	u_int speed = 0;

	if (!this->test_on_add ||
		this->tester->test_crypter(this->tester, algo, 0, create,
								   this->bench ? &speed : NULL,	plugin_name))
	{
		add_entry(this, this->crypters, algo, plugin_name, speed, create);
	}
}

METHOD(crypto_factory_t, remove_crypter, void,
	private_crypto_factory_t *this, crypter_constructor_t create)
{
	entry_t *entry;
	enumerator_t *enumerator;

	this->lock->write_lock(this->lock);
	enumerator = this->crypters->create_enumerator(this->crypters);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->create_crypter == create)
		{
			this->crypters->remove_at(this->crypters, enumerator);
			free(entry);
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
}

METHOD(crypto_factory_t, add_aead, void,
	private_crypto_factory_t *this,	encryption_algorithm_t algo,
	const char *plugin_name, aead_constructor_t create)
{
	u_int speed = 0;

	if (!this->test_on_add ||
		this->tester->test_aead(this->tester, algo, 0, create,
								this->bench ? &speed : NULL, plugin_name))
	{
		add_entry(this, this->aeads, algo, plugin_name, speed, create);
	}
}

METHOD(crypto_factory_t, remove_aead, void,
	private_crypto_factory_t *this, aead_constructor_t create)
{
	entry_t *entry;
	enumerator_t *enumerator;

	this->lock->write_lock(this->lock);
	enumerator = this->aeads->create_enumerator(this->aeads);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->create_aead == create)
		{
			this->aeads->remove_at(this->aeads, enumerator);
			free(entry);
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
}

METHOD(crypto_factory_t, add_signer, void,
	private_crypto_factory_t *this,	integrity_algorithm_t algo,
	const char *plugin_name, signer_constructor_t create)
{
	u_int speed = 0;

	if (!this->test_on_add ||
		this->tester->test_signer(this->tester, algo, create,
								  this->bench ? &speed : NULL, plugin_name))
	{
		add_entry(this, this->signers, algo, plugin_name, speed, create);
	}
}

METHOD(crypto_factory_t, remove_signer, void,
	private_crypto_factory_t *this, signer_constructor_t create)
{
	entry_t *entry;
	enumerator_t *enumerator;

	this->lock->write_lock(this->lock);
	enumerator = this->signers->create_enumerator(this->signers);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->create_signer == create)
		{
			this->signers->remove_at(this->signers, enumerator);
			free(entry);
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
}

METHOD(crypto_factory_t, add_hasher, void,
	private_crypto_factory_t *this,	hash_algorithm_t algo,
	const char *plugin_name, hasher_constructor_t create)
{
	u_int speed = 0;

	if (!this->test_on_add ||
		this->tester->test_hasher(this->tester, algo, create,
								  this->bench ? &speed : NULL, plugin_name))
	{
		add_entry(this, this->hashers, algo, plugin_name, speed, create);
	}
}

METHOD(crypto_factory_t, remove_hasher, void,
	private_crypto_factory_t *this, hasher_constructor_t create)
{
	entry_t *entry;
	enumerator_t *enumerator;

	this->lock->write_lock(this->lock);
	enumerator = this->hashers->create_enumerator(this->hashers);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->create_hasher == create)
		{
			this->hashers->remove_at(this->hashers, enumerator);
			free(entry);
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
}

METHOD(crypto_factory_t, add_prf, void,
	private_crypto_factory_t *this,	pseudo_random_function_t algo,
	const char *plugin_name, prf_constructor_t create)
{
	u_int speed = 0;

	if (!this->test_on_add ||
		this->tester->test_prf(this->tester, algo, create,
							   this->bench ? &speed : NULL, plugin_name))
	{
		add_entry(this, this->prfs, algo, plugin_name, speed, create);
	}
}

METHOD(crypto_factory_t, remove_prf, void,
	private_crypto_factory_t *this, prf_constructor_t create)
{
	entry_t *entry;
	enumerator_t *enumerator;

	this->lock->write_lock(this->lock);
	enumerator = this->prfs->create_enumerator(this->prfs);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->create_prf == create)
		{
			this->prfs->remove_at(this->prfs, enumerator);
			free(entry);
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
}

METHOD(crypto_factory_t, add_rng, void,
	private_crypto_factory_t *this, rng_quality_t quality,
	const char *plugin_name, rng_constructor_t create)
{
	u_int speed = 0;

	if (!this->test_on_add ||
		this->tester->test_rng(this->tester, quality, create,
							   this->bench ? &speed : NULL, plugin_name))
	{
		add_entry(this, this->rngs, quality, plugin_name, speed, create);
	}
}

METHOD(crypto_factory_t, remove_rng, void,
	private_crypto_factory_t *this, rng_constructor_t create)
{
	entry_t *entry;
	enumerator_t *enumerator;

	this->lock->write_lock(this->lock);
	enumerator = this->rngs->create_enumerator(this->rngs);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->create_rng == create)
		{
			this->rngs->remove_at(this->rngs, enumerator);
			free(entry);
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
}

METHOD(crypto_factory_t, add_dh, void,
	private_crypto_factory_t *this,	diffie_hellman_group_t group,
	 const char *plugin_name, dh_constructor_t create)
{
	add_entry(this, this->dhs, group, plugin_name, 0, create);
}

METHOD(crypto_factory_t, remove_dh, void,
	private_crypto_factory_t *this, dh_constructor_t create)
{
	entry_t *entry;
	enumerator_t *enumerator;

	this->lock->write_lock(this->lock);
	enumerator = this->dhs->create_enumerator(this->dhs);
	while (enumerator->enumerate(enumerator, &entry))
	{
		if (entry->create_dh == create)
		{
			this->dhs->remove_at(this->dhs, enumerator);
			free(entry);
		}
	}
	enumerator->destroy(enumerator);
	this->lock->unlock(this->lock);
}

/**
 * match algorithms of an entry?
 */
static bool entry_match(entry_t *a, entry_t *b)
{
	return a->algo == b->algo;
}

/**
 * check for uniqueness of an entry
 */
static bool unique_check(linked_list_t *list, entry_t **in, entry_t **out)
{
	if (list->find_first(list, (void*)entry_match, NULL, *in) == SUCCESS)
	{
		return FALSE;
	}
	*out = *in;
	list->insert_last(list, *in);
	return TRUE;
}

/**
 * create an enumerator over entry->algo in list with locking and unique check
 */
static enumerator_t *create_enumerator(private_crypto_factory_t *this,
									   linked_list_t *list, void *filter)
{
	this->lock->read_lock(this->lock);
	return enumerator_create_filter(
				enumerator_create_filter(
					list->create_enumerator(list), (void*)unique_check,
					linked_list_create(), (void*)list->destroy),
				filter,	this->lock, (void*)this->lock->unlock);
}

/**
 * Filter function to enumerate algorithm, not entry
 */
static bool crypter_filter(void *n, entry_t **entry, encryption_algorithm_t *algo,
						   void *i2, const char **plugin_name)
{
	*algo = (*entry)->algo;
	*plugin_name = (*entry)->plugin_name;
	return TRUE;
}

METHOD(crypto_factory_t, create_crypter_enumerator, enumerator_t*,
	private_crypto_factory_t *this)
{
	return create_enumerator(this, this->crypters, crypter_filter);
}

METHOD(crypto_factory_t, create_aead_enumerator, enumerator_t*,
	private_crypto_factory_t *this)
{
	return create_enumerator(this, this->aeads, crypter_filter);
}

/**
 * Filter function to enumerate algorithm, not entry
 */
static bool signer_filter(void *n, entry_t **entry, integrity_algorithm_t *algo,
						  void *i2, const char **plugin_name)
{
	*algo = (*entry)->algo;
	*plugin_name = (*entry)->plugin_name;
	return TRUE;
}

METHOD(crypto_factory_t, create_signer_enumerator, enumerator_t*,
	private_crypto_factory_t *this)
{
	return create_enumerator(this, this->signers, signer_filter);
}

/**
 * Filter function to enumerate algorithm, not entry
 */
static bool hasher_filter(void *n, entry_t **entry, hash_algorithm_t *algo,
						  void *i2, const char **plugin_name)
{
	*algo = (*entry)->algo;
	*plugin_name = (*entry)->plugin_name;
	return TRUE;
}

METHOD(crypto_factory_t, create_hasher_enumerator, enumerator_t*,
	private_crypto_factory_t *this)
{
	return create_enumerator(this, this->hashers, hasher_filter);
}

/**
 * Filter function to enumerate algorithm, not entry
 */
static bool prf_filter(void *n, entry_t **entry, pseudo_random_function_t *algo,
					   void *i2, const char **plugin_name)
{
	*algo = (*entry)->algo;
	*plugin_name = (*entry)->plugin_name;
	return TRUE;
}

METHOD(crypto_factory_t, create_prf_enumerator, enumerator_t*,
	private_crypto_factory_t *this)
{
	return create_enumerator(this, this->prfs, prf_filter);
}

/**
 * Filter function to enumerate algorithm, not entry
 */
static bool dh_filter(void *n, entry_t **entry, diffie_hellman_group_t *group,
					  void *i2, const char **plugin_name)
{
	*group = (*entry)->algo;
	*plugin_name = (*entry)->plugin_name;
	return TRUE;
}

METHOD(crypto_factory_t, create_dh_enumerator, enumerator_t*,
	private_crypto_factory_t *this)
{
	return create_enumerator(this, this->dhs, dh_filter);
}

/**
 * Filter function to enumerate algorithm, not entry
 */
static bool rng_filter(void *n, entry_t **entry, rng_quality_t *quality,
					   void *i2, const char **plugin_name)
{
	*quality = (*entry)->algo;
	*plugin_name = (*entry)->plugin_name;
	return TRUE;
}

METHOD(crypto_factory_t, create_rng_enumerator, enumerator_t*,
	private_crypto_factory_t *this)
{
	return create_enumerator(this, this->rngs, rng_filter);
}
METHOD(crypto_factory_t, add_test_vector, void,
	private_crypto_factory_t *this, transform_type_t type, void *vector)
{
	switch (type)
	{
		case ENCRYPTION_ALGORITHM:
			return this->tester->add_crypter_vector(this->tester, vector);
		case AEAD_ALGORITHM:
			return this->tester->add_aead_vector(this->tester, vector);
		case INTEGRITY_ALGORITHM:
			return this->tester->add_signer_vector(this->tester, vector);
		case HASH_ALGORITHM:
			return this->tester->add_hasher_vector(this->tester, vector);
		case PSEUDO_RANDOM_FUNCTION:
			return this->tester->add_prf_vector(this->tester, vector);
		case RANDOM_NUMBER_GENERATOR:
			return this->tester->add_rng_vector(this->tester, vector);
		default:
			DBG1(DBG_LIB, "%N test vectors not supported, ignored",
				 transform_type_names, type);
	}
}

METHOD(crypto_factory_t, destroy, void,
	private_crypto_factory_t *this)
{
	this->crypters->destroy(this->crypters);
	this->aeads->destroy(this->aeads);
	this->signers->destroy(this->signers);
	this->hashers->destroy(this->hashers);
	this->prfs->destroy(this->prfs);
	this->rngs->destroy(this->rngs);
	this->dhs->destroy(this->dhs);
	this->tester->destroy(this->tester);
	this->lock->destroy(this->lock);
	free(this);
}

/*
 * see header file
 */
crypto_factory_t *crypto_factory_create()
{
	private_crypto_factory_t *this;

	INIT(this,
		.public = {
			.create_crypter = _create_crypter,
			.create_aead = _create_aead,
			.create_signer = _create_signer,
			.create_hasher = _create_hasher,
			.create_prf = _create_prf,
			.create_rng = _create_rng,
			.create_dh = _create_dh,
			.add_crypter = _add_crypter,
			.remove_crypter = _remove_crypter,
			.add_aead = _add_aead,
			.remove_aead = _remove_aead,
			.add_signer = _add_signer,
			.remove_signer = _remove_signer,
			.add_hasher = _add_hasher,
			.remove_hasher = _remove_hasher,
			.add_prf = _add_prf,
			.remove_prf = _remove_prf,
			.add_rng = _add_rng,
			.remove_rng = _remove_rng,
			.add_dh = _add_dh,
			.remove_dh = _remove_dh,
			.create_crypter_enumerator = _create_crypter_enumerator,
			.create_aead_enumerator = _create_aead_enumerator,
			.create_signer_enumerator = _create_signer_enumerator,
			.create_hasher_enumerator = _create_hasher_enumerator,
			.create_prf_enumerator = _create_prf_enumerator,
			.create_dh_enumerator = _create_dh_enumerator,
			.create_rng_enumerator = _create_rng_enumerator,
			.add_test_vector = _add_test_vector,
			.destroy = _destroy,
		},
		.crypters = linked_list_create(),
		.aeads = linked_list_create(),
		.signers = linked_list_create(),
		.hashers = linked_list_create(),
		.prfs = linked_list_create(),
		.rngs = linked_list_create(),
		.dhs = linked_list_create(),
		.lock = rwlock_create(RWLOCK_TYPE_DEFAULT),
		.tester = crypto_tester_create(),
		.test_on_add = lib->settings->get_bool(lib->settings,
								"libstrongswan.crypto_test.on_add", FALSE),
		.test_on_create = lib->settings->get_bool(lib->settings,
								"libstrongswan.crypto_test.on_create", FALSE),
		.bench = lib->settings->get_bool(lib->settings,
								"libstrongswan.crypto_test.bench", FALSE),
	);

	return &this->public;
}

