/*
 * Copyright (C) 2012 Andreas Steffen
 * HSR Hochschule fuer Technik Rapperswil
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

#include "imv_os_database.h"

#include <utils/debug.h>

typedef struct private_imv_os_database_t private_imv_os_database_t;

/**
 * Private data of a imv_os_database_t object.
 *
 */
struct private_imv_os_database_t {

	/**
	 * Public imv_os_database_t interface.
	 */
	imv_os_database_t public;

	/**
	 * database instance
	 */
	database_t *db;

};

METHOD(imv_os_database_t, check_packages, status_t,
	private_imv_os_database_t *this, imv_os_state_t *state,
	enumerator_t *package_enumerator)
{
	char *product, *package, *release, *cur_release;
	u_char *pos;
	chunk_t os_name, os_version, name, version;
	os_type_t os_type;
	size_t os_version_len;
	int pid, gid, security;
	int count = 0, count_ok = 0, count_no_match = 0, count_not_found = 0;
	enumerator_t *e;
	status_t status = SUCCESS;
	bool found, match;

	state->get_info(state, &os_type, &os_name, &os_version);

	if (os_type == OS_TYPE_ANDROID)
	{
		/*no package dependency on Android version */
		os_version_len = 0;
	}
	else
	{
		/* remove appended platform info */
		pos = memchr(os_version.ptr, ' ', os_version.len);
		os_version_len = pos ? (pos - os_version.ptr) : os_version.len;
	}

	product = malloc(os_name.len + 1 + os_version_len + 1);
	sprintf(product, "%.*s %.*s", os_name.len, os_name.ptr,
								  os_version_len, os_version.ptr); 

	/* Get primary key of product */
	e = this->db->query(this->db,
				"SELECT id FROM products WHERE name = ?",
				DB_TEXT, product, DB_INT);
	if (!e)
	{
		free(product);
		return FAILED;
	}
	if (!e->enumerate(e, &pid))
	{
		e->destroy(e);
		free(product);
		return NOT_FOUND;
	}
	e->destroy(e);

	while (package_enumerator->enumerate(package_enumerator, &name, &version))
	{
		/* Convert package name chunk to a string */
		package = malloc(name.len + 1);
		memcpy(package, name.ptr, name.len);
		package[name.len] = '\0';
		count++;

		/* Get primary key of package */
		e = this->db->query(this->db,
					"SELECT id FROM packages WHERE name = ?",
					DB_TEXT, package, DB_INT);
		if (!e)
		{
			free(product);
			free(package);
			return FAILED;
		}
		if (!e->enumerate(e, &gid))
		{
			/* package not present in database for any product - skip */
			if (os_type == OS_TYPE_ANDROID)
			{
				DBG2(DBG_IMV, "package '%s' (%.*s) not found",
					 package, version.len, version.ptr);
			}
			count_not_found++;
			e->destroy(e);
			continue;
		}
		e->destroy(e);

		/* Convert package version chunk to a string */
		release = malloc(version.len + 1);
		memcpy(release, version.ptr, version.len);
		release[version.len] = '\0';

		/* Enumerate over all acceptable versions */
		e = this->db->query(this->db,
				"SELECT release, security FROM versions "
				"WHERE product = ? AND package = ?",
				DB_INT, pid, DB_INT, gid, DB_TEXT, DB_INT);
		if (!e)
		{
			free(product);
			free(package);
			free(release);
			return FAILED;
		}
		found = FALSE;
		match = FALSE;

		while (e->enumerate(e, &cur_release, &security))
		{
			found = TRUE;
			if (streq(release, cur_release))
			{
				match = TRUE;
				break;
			}
		}
		e->destroy(e);
		
		if (found)
		{
			if (match)
			{
				DBG2(DBG_IMV, "package '%s' (%s)%s is ok", package, release,
							   security ? " [s]" : "");
				count_ok++;
			}
			else
			{
				DBG1(DBG_IMV, "package '%s' (%s) no match", package, release);
				count_no_match++;
				status = VERIFY_ERROR;
			}
		}
		else
		{
			/* package not present in database for this product - skip */
			count_not_found++;
		}
		free(package);
		free(release);
	}
	free(product);

	DBG1(DBG_IMV, "processed %d packages: %d no match, %d ok, %d not found",
		 count, count_no_match, count_ok, count_not_found);

	return status;
}

METHOD(imv_os_database_t, destroy, void,
	private_imv_os_database_t *this)
{
	this->db->destroy(this->db);
	free(this);
}

/**
 * See header
 */
imv_os_database_t *imv_os_database_create(char *uri)
{
	private_imv_os_database_t *this;

	INIT(this,
		.public = {
			.check_packages = _check_packages,
			.destroy = _destroy,
		},
		.db = lib->db->create(lib->db, uri),
	);

	if (!this->db)
	{
		DBG1(DBG_IMV,
			 "failed to connect to OS database '%s'", uri);
		free(this);
		return NULL;
	}

	return &this->public;
}
