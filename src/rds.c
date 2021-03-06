/*
   Tagsistant (tagfs) -- rds.c
   Copyright (C) 2006-2013 Tx0 <tx0@strumentiresistenti.org>

   Manage Reusable Data Sets including query results.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "tagsistant.h"

/************************************************************************************/
/***                                                                              ***/
/*** FileTree translation                                                         ***/
/***                                                                              ***/
/************************************************************************************/

/**
 * add a file to the file tree (callback function)
 *
 * @param hash_table_pointer a GHashTable to hold results
 * @param result a DBI result
 */
static int tagsistant_add_to_fileset(void *hash_table_pointer, dbi_result result)
{
	/* Cast the hash table */
	GHashTable *hash_table = (GHashTable *) hash_table_pointer;

	/* fetch query results */
	gchar *name = dbi_result_get_string_copy_idx(result, 1);
	if (!name) return (0);

	tagsistant_inode inode = dbi_result_get_uint_idx(result, 2);

	/* lookup the GList object */
	GList *list = g_hash_table_lookup(hash_table, name);

	/* look for duplicates due to reasoning results */
	GList *list_tmp = list;
	while (list_tmp) {
		tagsistant_file_handle *fh_tmp = (tagsistant_file_handle *) list_tmp->data;

		if (fh_tmp && (fh_tmp->inode == inode)) {
			g_free_null(name);
			return (0);
		}

		list_tmp = list_tmp->next;
	}

	/* fetch query results into tagsistant_file_handle struct */
	tagsistant_file_handle *fh = g_new0(tagsistant_file_handle, 1);
	if (!fh) {
		g_free_null(name);
		return (0);
	}

	g_strlcpy(fh->name, name, 1024);
	fh->inode = inode;
	g_free_null(name);

	/* add the new element */
	// TODO valgrind says: check for leaks
	g_hash_table_insert(hash_table, g_strdup(fh->name), g_list_prepend(list, fh));

//	dbg('f', LOG_INFO, "adding (%d,%s) to filetree", fh->inode, fh->name);

	return (0);
}

/**
 * Add a filter criterion to a WHERE clause based on a qtree_and_node object
 *
 * @param statement a GString object holding the building query statement
 * @param and_set the qtree_and_node object describing the tag to be added as a criterion
 */
void tagsistant_query_add_and_set(GString *statement, qtree_and_node *and_set)
{
	if (and_set->value && strlen(and_set->value)) {
		switch (and_set->operator) {
			case TAGSISTANT_EQUAL_TO:
				g_string_append_printf(statement,
					"tagname = \"%s\" and `key` = \"%s\" and value = \"%s\" ",
					and_set->namespace,
					and_set->key,
					and_set->value);
				break;
			case TAGSISTANT_CONTAINS:
				g_string_append_printf(statement,
					"tagname = \"%s\" and `key` = \"%s\" and value like '%%%s%%' ",
					and_set->namespace,
					and_set->key,
					and_set->value);
				break;
			case TAGSISTANT_GREATER_THAN:
				g_string_append_printf(statement,
					"tagname = \"%s\" and `key` = \"%s\" and value > \"%s\" ",
					and_set->namespace,
					and_set->key,
					and_set->value);
				break;
			case TAGSISTANT_SMALLER_THAN:
				g_string_append_printf(statement,
					"tagname = \"%s\" and `key` = \"%s\" and value < \"%s\" ",
					and_set->namespace,
					and_set->key,
					and_set->value);
				break;
		}
	} else if (and_set->tag) {
		g_string_append_printf(statement, "tagname = \"%s\" ", and_set->tag);
	} else if (and_set->tag_id) {
		g_string_append_printf(statement, "tagging.tag_id = %d ", and_set->tag_id);
	}
}

gchar *tagsistant_rds_path(tagsistant_querytree *qtree)
{
	gchar *path = g_strdup(qtree->expanded_full_path);
	gchar *delimiter = strstr(path, TAGSISTANT_QUERY_DELIMITER);
	if (delimiter) { *delimiter = '\0'; }
	return (path);
}

/*
 * Lookup the id of the RDS of a query.
 *
 * @param query the query that generates the RDS
 * @return the RDS id
 */
int tagsistant_get_rds_id(tagsistant_querytree *qtree)
{
	int id = 0;

	/*
	 * Extract the path to be looked up
	 */
	gchar *path = tagsistant_rds_path(qtree);

	/*
	 * Lookup the id, if the RDS was previously created
	 */
	tagsistant_query(
		"select id from rds_index where query = \"%s\"",
		qtree->dbi, tagsistant_return_integer, &id, path);

	g_free(path);

	return (id);
}

/*
 * Save a source tag in the RDS source table
 *
 * @param node the source tag
 * @param rds_id the RDS id
 * @param dbi a DBI connection
 */
void
tagsistant_register_rds_source(qtree_and_node *node, int rds_id, dbi_conn dbi)
{
	gchar *tag = (node->tag && strlen(node->tag)) ? node->tag : node->namespace;

	tagsistant_query(
		"insert into rds_tags (id, tag) values (%d, \"%s\")",
		dbi, NULL, NULL, rds_id, tag);
}

/*
 * Create a new RDS for the query and return its id.
 *
 * @param query the query that generates the RDS
 * @return the RDS id
 */
int tagsistant_create_rds(tagsistant_querytree *qtree)
{
	int id = 0;

	/*
	 * Extract the path to be looked up
	 */
	gchar *path = tagsistant_rds_path(qtree);

	/*
	 * Create a new entry and fetch the last insert id
	 */
	tagsistant_query(
		"insert into rds_index (query) values (\"%s\")",
		qtree->dbi, NULL, NULL, path);

	id = tagsistant_last_insert_id(qtree->dbi);

	g_free(path);

	/*
	 * For every subquery, save the source tags
	 */
	qtree_or_node *query = qtree->tree;
	while (query) {
		/*
		 * Register every tag...
		 */
		qtree_and_node *and = query->and_set;

		while (and) {
			tagsistant_register_rds_source(and, id, qtree->dbi);

			/*
			 * ... and every related tag ...
			 */
			qtree_and_node *related = and->related;
			while (related) {
				tagsistant_register_rds_source(related, id, qtree->dbi);
				related = related->related;
			}

			/*
			 * ... and even negated tags.
			 */
			qtree_and_node *negated = and->negated;
			while (negated) {
				tagsistant_register_rds_source(negated, id, qtree->dbi);
				negated = negated->negated;
			}

			and = and->next;
		}
		query = query->next;
	}

	return (id);
}

/*
 * Materialize the RDS of a query
 *
 * @param qtree the querytree object
 */
int tagsistant_materialize_rds(tagsistant_querytree *qtree)
{
	qtree_or_node *query = qtree->tree;

	/*
	 * PHASE 1.
	 * Build a set of temporary tables containing all the matched objects
	 *
	 * Step 1.1. for each qtree_or_node build a temporary table
	 */
	while (query) {
		GString *create_base_table = g_string_sized_new(51200);
		g_string_append_printf(create_base_table,
			"create temporary table tv%.16" PRIxPTR " as "
			"select objects.inode, objects.objectname from objects "
				"join tagging on tagging.inode = objects.inode "
				"join tags on tags.tag_id = tagging.tag_id "
				"where ",
			(uintptr_t) query);

		/*
		 * add each qtree_and_node (main and ->related) to the query
		 */
		tagsistant_query_add_and_set(create_base_table, query->and_set);

		qtree_and_node *related = query->and_set->related;
		while (related) {
			g_string_append(create_base_table, " or ");
			tagsistant_query_add_and_set(create_base_table, related);
			related = related->related;
		}

		/*
		 * create the table and dispose the statement GString
		 */
		tagsistant_query(create_base_table->str, qtree->dbi, NULL, NULL);
		g_string_free(create_base_table, TRUE);

		/*
		 * Step 1.2.
		 * for each ->next linked node, subtract from the base table
		 * the objects not matching this node
		 */
		qtree_and_node *next = query->and_set->next;
		while (next) {
			GString *cross_tag = g_string_sized_new(51200);
			g_string_append_printf(cross_tag,
				"delete from tv%.16" PRIxPTR " where inode not in ("
				"select objects.inode from objects "
					"join tagging on tagging.inode = objects.inode "
					"join tags on tags.tag_id = tagging.tag_id "
					"where ",
				(uintptr_t) query);

			/*
			 * add each qtree_and_node (main and ->related) to the query
			 */
			tagsistant_query_add_and_set(cross_tag, next);

			qtree_and_node *related = next->related;
			while (related) {
				g_string_append(cross_tag, " or ");
				tagsistant_query_add_and_set(cross_tag, related);
				related = related->related;
			}

			/*
			 * close the subquery
			 */
			g_string_append(cross_tag, ")");

			/*
			 * apply the query and dispose the statement GString
			 */
			tagsistant_query(cross_tag->str, qtree->dbi, NULL, NULL);
			g_string_free(cross_tag, TRUE);

			next = next->next;
		}

		/*
		 * Step 1.3.
		 * for each ->negated linked node, subtract from the base table
		 * the objects that do match this node
		 */
		next = query->and_set;
		while (next) {
			qtree_and_node *negated = next->negated;
			while (negated) {
				GString *cross_tag = g_string_sized_new(51200);
				g_string_append_printf(cross_tag,
					"delete from tv%.16" PRIxPTR " where inode in ("
					"select objects.inode from objects "
						"join tagging on tagging.inode = objects.inode "
						"join tags on tags.tag_id = tagging.tag_id "
						"where ",
					(uintptr_t) query);

				/*
				 * add each qtree_and_node (main and ->related) to the query
				 */
				tagsistant_query_add_and_set(cross_tag, negated);

				qtree_and_node *related = negated->related;
				while (related) {
					g_string_append(cross_tag, " or ");
					tagsistant_query_add_and_set(cross_tag, related);
					related = related->related;
				}

				/*
				 * close the subquery
				 */
				g_string_append(cross_tag, ")");

				/*
				 * apply the query and dispose the statement GString
				 */
				tagsistant_query(cross_tag->str, qtree->dbi, NULL, NULL);
				g_string_free(cross_tag, TRUE);

				negated = negated->negated;
			}

			next = next->next;
		}

		/*
		 * move to the next qtree_or_node in the linked list
		 */
		query = query->next;
	}

	/*
	 * PHASE 2.
	 *
	 * Create a new RDS
	 */
	int rds_id = tagsistant_create_rds(qtree);

	/*
	 * format the main statement which reads from the temporary
	 * tables using UNION and ordering the files
	 */
	GString *view_statement = g_string_sized_new(10240);

	query = qtree->tree;

	while (query) {
		g_string_append_printf(view_statement, "select %d, inode, objectname from tv%.16" PRIxPTR, rds_id, (uintptr_t) query);
		if (query->next) g_string_append(view_statement, " union ");
		query = query->next;
	}

	/*
	 * Load all the files in the RDS
	 */
	tagsistant_query(
		"insert into rds %s",
		qtree->dbi, NULL, NULL, view_statement->str);

	/*
	 * free the SQL statement
	 */
	g_string_free(view_statement, TRUE);

	/*
	 * PHASE 3.
	 *
	 * drop the temporary tables
	 */
	query = qtree->tree;
	while (query) {
		tagsistant_query("drop table tv%.16" PRIxPTR, qtree->dbi, NULL, NULL, (uintptr_t) query);
		query = query->next;
	}

	return (rds_id);
}

/**
 * build a linked list of filenames that satisfy the querytree
 * object. This is translated in a two phase flow:
 *
 * 1. each qtree_and_node list is translated into one
 * (temporary) table
 *
 * 2. the content of all tables are read in with a UNION
 * chain inside a super-select to apply the ORDER BY clause.
 *
 * 3. all the (temporary) tables are removed
 *
 * @param query the qtree_or_node query structure to be resolved
 * @param conn a libDBI dbi_conn handle
 * @param is_all_path is true when the path includes the ALL/ tag
 * @return a pointer to a GHashTable of tagsistant_file_handle objects
 */
GHashTable *tagsistant_rds_new(tagsistant_querytree *qtree, int is_all_path)
{
	/*
	 * If the query contains the ALL meta-tag, just select all the available
	 * objects and return them
	 */
	if (is_all_path) {
		GHashTable *file_hash = g_hash_table_new(g_str_hash, g_str_equal);

		tagsistant_query(
			"select objectname, inode from objects",
			qtree->dbi, tagsistant_add_to_fileset, file_hash);

		return(file_hash);
	}

	/*
	 * a NULL query can't be processed
	 */
	if (!qtree) {
		dbg('f', LOG_ERR, "NULL tagsistant_querytree object provided to %s", __func__);
		return(NULL);
	}

	qtree_or_node *query = qtree->tree;

	if (!query) {
		dbg('f', LOG_ERR, "NULL qtree_or_node object provided to %s", __func__);
		return(NULL);
	}

	/*
	 * Get the RDS id
	 */
	int rds_id = tagsistant_get_rds_id(qtree);

	/*
	 * If the rds_id is not zero, load the objects from the RDS and return them
	 */
	if (rds_id) {
		GHashTable *file_hash = g_hash_table_new(g_str_hash, g_str_equal);

		tagsistant_query(
			"select objectname, inode from rds where id = %d",
			qtree->dbi, tagsistant_add_to_fileset, file_hash, rds_id);

		return (file_hash);
	}

	/*
	 * materialize the RDS
	 */
	rds_id = tagsistant_materialize_rds(qtree);

	/*
	 * load the RDS content and return it
	 */
	GHashTable *file_hash = g_hash_table_new(g_str_hash, g_str_equal);
	tagsistant_query(
		"select objectname, inode from rds where id = %d",
		qtree->dbi, tagsistant_add_to_fileset, file_hash, rds_id);

	return(file_hash);
}

/**
 * Destroy a filetree element GList list of tagsistant_file_handle.
 * This will free the GList data structure by first calling
 * tagsistant_filetree_destroy_value() on each linked node.
 */
void tagsistant_rds_destroy_value_list(gchar *key, GList *list, gpointer data)
{
	(void) data;

	g_free_null(key);

	if (list) g_list_free_full(list, (GDestroyNotify) g_free /* was tagsistant_filetree_destroy_value */);
}

int tagsistant_delete_rds_by_source_callback(void *data, dbi_result result)
{
	(void) data;

	// complete here!

	return (0);
}

void tagsistant_delete_rds_by_source(qtree_and_node *node, dbi_conn dbi)
{
	gchar *tag = (node->tag && strlen(node->tag)) ? node->tag : node->namespace;

	tagsistant_query(
		"insert into rds_tags (id, tag) values (%d, \"%s\")",
		dbi, tagsistant_delete_rds_by_source_callback, NULL, tag);
}

/*
 * Deletes every RDS involved with one query
 *
 * @param query the query driving the deletion
 */
void tagsistant_delete_rds_involved(tagsistant_querytree *qtree)
{
	tagsistant_query("delete from rds", qtree->dbi, NULL, NULL);
	tagsistant_query("delete from rds_index", qtree->dbi, NULL, NULL);
	tagsistant_query("delete from rds_tags", qtree->dbi, NULL, NULL);

	return;

	/*
	 * For every subquery, save the source tags
	 */
	qtree_or_node *query = qtree->tree;
	while (query) {
		/*
		 * Register every tag...
		 */
		qtree_and_node *and = query->and_set;

		while (and) {
			tagsistant_delete_rds_by_source(and, qtree->dbi);

			/*
			 * ... and every related tag ...
			 */
			qtree_and_node *related = and->related;
			while (related) {
				tagsistant_delete_rds_by_source(related, qtree->dbi);
				related = related->related;
			}

			/*
			 * ... and even negated tags.
			 */
			qtree_and_node *negated = and->negated;
			while (negated) {
				tagsistant_delete_rds_by_source(negated, qtree->dbi);
				negated = negated->negated;
			}

			and = and->next;
		}
		query = query->next;
	}
}
