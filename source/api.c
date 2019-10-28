#include <sabi/namespace.h>
#include <sabi/operand.h>
#include <sabi/parser.h>
#include <sabi/fields.h>
#include <sabi/state.h>
#include <sabi/host.h>
#include <sabi/api.h>
#include <string.h>

static void convert_path(sabi_name_t *name, const char *path)
{
	uint8_t *prefix, *value;
	int count;

	count = 0;
	prefix = (uint8_t*)name->prefix;
	value = (uint8_t*)name->value;

	while(*path == '\\' || *path == '^')
	{
		*prefix++ = *path++;
	}

	while(*path)
	{
		if(*path == '.')
		{
			while(count % 4)
			{
				*value++ = '_';
				count++;
			}
		}
		else
		{
			*value++ = *path;
			count++;
		}
		path++;
	}

	while(count % 4)
	{
		*value++ = '_';
		count++;
	}

	*prefix = '\0';
	*value = '\0';
}

static uint8_t char_to_hex(uint8_t c)
{
	if(c >= '0' && c <= '9')
	{
		return (c - '0');
	}
	else if(c >= 'A' && c <= 'F')
	{
		return (c - 'A' + 10);
	}
	else if(c >= 'a' && c <= 'f')
	{
		return (c - 'a' + 10);
	}
	return 0;
}

sabi_node_t *sabi_resolve_child(sabi_node_t *parent, const char *name)
{
	sabi_name_t fqn;
	convert_path(&fqn, name);
	return sabi_ns_exists(parent, fqn.value);
}

sabi_node_t *sabi_resolve_search(sabi_node_t *parent, const char *name)
{
	sabi_name_t fqn;
	convert_path(&fqn, name);
	return sabi_ns_search(parent, fqn.value);
}

sabi_node_t *sabi_resolve_path(sabi_node_t *parent, const char *path)
{
	sabi_name_t fqn;
	convert_path(&fqn, path);
	if(fqn.prefix[0] == '\0')
	{
		fqn.prefix[0] = 'x';
	}
	return sabi_ns_find(parent, &fqn);
}

sabi_node_t *sabi_next_node(sabi_node_t *node, int child)
{
	if(node == 0)
	{
		node = sabi_ns_root();
		return node->child;
	}

	if(child && node->child)
	{
		node = node->child;
	}
	else if(node->next)
	{
		node = node->next;
	}
	else
	{
		while(node->next == 0)
		{
			node = node->parent;
			if(node == 0)
			{
				break;
			}
		}
		if(node)
		{
			node = node->next;
		}
	}

	return node;
}

int sabi_eval_method(sabi_node_t *node, int argc, sabi_data_t *argv, sabi_data_t *retv)
{
	sabi_object_t *object;
	state_t *state;
	uint8_t count;

	object = &node->object;
	count = object->method.argc;
	if(retv)
	{
		retv->type = SABI_DATA_NULL;
	}

	if(object->type != SABI_OBJECT_METHOD)
	{
		return -SABI_ERROR_TYPE;
	}

	if(count != argc)
	{
		return -SABI_ERROR_ARGS;
	}

	state = sabi_new_state();
	state->argc = count;
	state->scope = node;
	state->aml = object->method.start;

	for(int i = 0; i < count; i++)
	{
		sabi_clone_data(state->argv+i, argv+i);
	}

	sabi_parse_termlist(state, object->method.end);

	if(retv)
	{
		sabi_clone_data(retv, state->retv);
	}

	sabi_free_state(state);
	return 0;
}

int sabi_eval_node(sabi_node_t *node, sabi_data_t *retv)
{
	sabi_object_t *object;
	operand_t operand;
	sabi_data_t temp;
	int type;

	retv->type = SABI_DATA_NULL;
	object = &node->object;
	type = object->type;

	if(type == SABI_OBJECT_METHOD)
	{
		if(object->method.argc)
		{
			return -SABI_ERROR_ARGS;
		}
		else
		{
			return sabi_eval_method(node, 0, 0, retv);
		}
	}

	if(type == SABI_OBJECT_NAME)
	{
		operand.flags = OPERAND_DATA;
		operand.refof = object->name.data;
	}
	else if((type == SABI_OBJECT_FIELD) || (type == SABI_OBJECT_INDEX_FIELD))
	{
		operand.flags = OPERAND_OBJECT;
		operand.refof = object;
	}
	else
	{
		return -SABI_ERROR_TYPE;
	}

	sabi_read_operand(&operand, &temp);
	sabi_clone_data(retv, &temp);

	return 0;
}

void sabi_clean_data(sabi_data_t *sptr)
{
	sabi_data_t *data;
	int type, count;

	type = sptr->type;
	sptr->type = SABI_DATA_NULL;

	if(type == SABI_DATA_PACKAGE)
	{
		data = sptr->package.data;
		count = sptr->package.count;
		for(int i = 0; i < count; i++)
		{
			sabi_clean_data(data+i);
		}
		sabi_host_free(data);
	}
	else if(type == SABI_DATA_BUFFER)
	{
		sabi_host_free(sptr->buffer.ptr);
	}
}

void sabi_clone_data(sabi_data_t *dptr, sabi_data_t *sptr)
{
	sabi_data_t *data;
	int type, count;
	uint64_t value;

	if(dptr == sptr)
	{
		return;
	}

	*dptr = *sptr;
	type = sptr->type;

	if(type == SABI_DATA_PACKAGE)
	{
		count = sptr->package.count;
		data = sabi_host_alloc(count, sizeof(sabi_data_t));
		for(int i = 0; i < count; i++)
		{
			sabi_clone_data(data+i, sptr->package.data+i);
		}
		dptr->package.data = data;
	}
	else if(type == SABI_DATA_BUFFER)
	{
		count = sptr->buffer.size;
		dptr->buffer.ptr = sabi_host_alloc(1, count);
		memcpy(dptr->buffer.ptr, sptr->buffer.ptr, count);
	}
	else if(type == SABI_DATA_BUFFER_FIELD)
	{
		sabi_access_buffer_field(sptr, &value, 0);
		dptr->integer.type = SABI_DATA_INTEGER;
		dptr->integer.value = value;
	}
}

uint32_t sabi_eisaid(const char *id)
{
	uint32_t value, swap;
	uint8_t *bp;

	value = (id[0] - 0x40);
	value = (value << 5) | (id[1] - 0x40);
	value = (value << 5) | (id[2] - 0x40);
	value = (value << 4) | char_to_hex(id[3]);
	value = (value << 4) | char_to_hex(id[4]);
	value = (value << 4) | char_to_hex(id[5]);
	value = (value << 4) | char_to_hex(id[6]);

	bp = (uint8_t*)&value;
	swap = bp[0];
	swap = (swap << 8) | bp[1];
	swap = (swap << 8) | bp[2];
	swap = (swap << 8) | bp[3];

	return swap;
}

int sabi_check_pnp_id(sabi_node_t *parent, uint32_t id)
{
	sabi_node_t *node;
	sabi_data_t data;
	uint32_t value;

	node = sabi_ns_exists(parent, "_HID");
	if(node)
	{
		sabi_eval_node(node, &data);
		value = data.integer.value;
		sabi_clean_data(&data);
		if(value == id)
		{
			return 1;
		}
	}

	node = sabi_ns_exists(parent, "_CID");
	if(node)
	{
		sabi_eval_node(node, &data);
		value = data.integer.value;
		sabi_clean_data(&data);
		if(value == id)
		{
			return 1;
		}
	}

	return 0;
}
