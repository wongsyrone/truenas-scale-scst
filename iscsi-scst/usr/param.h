/*
 *  Copyright (C) 2005 FUJITA Tomonori <tomof@acm.org>
 *  Copyright (C) 2007 - 2018 Vladislav Bolkhovitin
 *  Copyright (C) 2007 - 2018 Western Digital Corporation
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#ifndef PARAMS_H
#define PARAMS_H

#define ISCSI_ISNS_SERVER_ATTR_NAME		"iSNSServer"
#define ISCSI_ISNS_ACCESS_CONTROL_ATTR_NAME	"iSNSAccessControl"
#define ISCSI_ENABLED_ATTR_NAME			"enabled"
#define ISCSI_ISNS_ENTITY_ATTR_NAME			"isns_entity_name"
#define ISCSI_ALLOWED_PORTAL_ATTR_NAME		"allowed_portal"
#define ISCSI_INTERNAL_PORTAL_ATTR_NAME		"internal_portal"
#define ISCSI_PER_PORTAL_ACL_ATTR_NAME		"per_portal_acl"
#define ISCSI_TARGET_REDIRECTION_ATTR_NAME	"redirect"
#define ISCSI_TARGET_REDIRECTION_VALUE_TEMP	"temp"
#define ISCSI_TARGET_REDIRECTION_VALUE_PERM	"perm"
#define ISCSI_TARGET_ALIAS_ATTR_NAME		"alias"
#define ISCSI_LINK_LOCAL_ATTR_NAME		"link_local"

struct iscsi_key;

struct iscsi_param {
	int key_state;
	unsigned int val;
};

struct iscsi_key_ops {
	int (*val_to_str)(unsigned int, char *, int);
	int (*str_to_val)(const char *, unsigned int *);
	int (*check_val)(const struct iscsi_key *, unsigned int *);
	int (*set_val)(struct iscsi_param *, int, unsigned int *);
};

struct iscsi_key {
	const char *name;
	unsigned int rfc_def;
	unsigned int local_def;
	unsigned int min;
	unsigned int max;
	int show_in_sysfs;
	struct iscsi_key_ops *ops;
};

extern struct iscsi_key session_keys[];
extern struct iscsi_key target_keys[];
extern struct iscsi_key user_keys[];

extern size_t strlcpy(char *dest, const char *src, size_t size);

extern void params_set_defaults(unsigned int *, const struct iscsi_key *);
extern int params_index_by_name(const char *, const struct iscsi_key *);
extern int params_index_by_name_numwild(const char *, const struct iscsi_key *);
extern int params_val_to_str(const struct iscsi_key *, int, unsigned int,
			     char *, int);
extern int params_str_to_val(const struct iscsi_key *, int, const char *,
			     unsigned int *);
extern int params_check_val(const struct iscsi_key *, int, unsigned int *);
extern int params_set_val(struct iscsi_key *, struct iscsi_param *, int,
			  unsigned int *);

#endif
