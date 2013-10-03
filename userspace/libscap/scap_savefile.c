#ifndef _WIN32
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "scap.h"
#include "scap-int.h"
#include "scap_savefile.h"

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// WRITE FUNCTIONS
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
static inline uint32_t scap_normalize_block_len(uint32_t blocklen)
{
	return ((blocklen + 3) >> 2) << 2;
}

static int32_t scap_write_padding(FILE *f, uint32_t blocklen)
{
	int32_t val = 0;
	int32_t bytestowrite = scap_normalize_block_len(blocklen) - blocklen;

	if(fwrite(&val, 1, bytestowrite, f) == bytestowrite)
	{
		return SCAP_SUCCESS;
	}
	else
	{
		return SCAP_FAILURE;
	}
}

static int32_t scap_write_proc_fds(scap_t *handle, struct scap_threadinfo *tinfo, FILE *f)
{
	block_header bh;
	uint32_t bt;
	uint32_t totlen = MEMBER_SIZE(scap_threadinfo, tid);  // This includes the tid
	struct scap_fdinfo *fdi;
	struct scap_fdinfo *tfdi;

	//
	// First pass of the table to calculate the length
	//
	HASH_ITER(hh, tinfo->fdlist, fdi, tfdi)
	{
		totlen += scap_fd_info_len(fdi);
	}

	//
	// Create the block
	//
	bh.block_type = FDL_BLOCK_TYPE;
	bh.block_total_length = scap_normalize_block_len(sizeof(block_header) + totlen + 4);

	if(fwrite(&bh, sizeof(bh), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (fd1)");
		return SCAP_FAILURE;
	}

	//
	// Write the tid
	//
	if(fwrite(&tinfo->tid, sizeof(tinfo->tid), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (fd2)");
		return SCAP_FAILURE;
	}

	//
	// Second pass pass of the table to dump it
	//
	HASH_ITER(hh, tinfo->fdlist, fdi, tfdi)
	{
		if(scap_fd_write_to_disk(handle, fdi, f) != SCAP_SUCCESS)
		{
			return SCAP_FAILURE;
		}
	}

	//
	// Add the padding
	//
	if(scap_write_padding(f, totlen) != SCAP_SUCCESS)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (fd3)");
		return SCAP_FAILURE;
	}

	//
	// Create the trailer
	//
	bt = bh.block_total_length;
	if(fwrite(&bt, sizeof(bt), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (fd4)");
		return SCAP_FAILURE;
	}

	return SCAP_SUCCESS;
}

//
// Write the fd list blocks
//
int32_t scap_write_fdlist(scap_t *handle, FILE *f)
{
	struct scap_threadinfo *tinfo;
	struct scap_threadinfo *ttinfo;
	int32_t res;

	HASH_ITER(hh, handle->m_proclist, tinfo, ttinfo)
	{
		res = scap_write_proc_fds(handle, tinfo, f);
		if(res != SCAP_SUCCESS)
		{
			return res;
		}
	}

	return SCAP_SUCCESS;
}

//
// Write the process list block
//
int32_t scap_write_proclist(scap_t *handle, FILE *f)
{
	block_header bh;
	uint32_t bt;
	uint32_t totlen = 0;
	struct scap_threadinfo *tinfo;
	struct scap_threadinfo *ttinfo;
	uint16_t commlen;
	uint16_t exelen;
	uint16_t argslen;
	uint16_t cwdlen;

	//
	// First pass pass of the table to calculate the length
	//
	HASH_ITER(hh, handle->m_proclist, tinfo, ttinfo)
	{
		totlen +=
		    sizeof(uint64_t) +	// tid
		    sizeof(uint64_t) +	// pid
		    sizeof(uint64_t) +	// ppid
		    2 + strnlen(tinfo->comm, SCAP_MAX_PATH_SIZE) +
		    2 + strnlen(tinfo->exe, SCAP_MAX_PATH_SIZE) +
		    2 + tinfo->args_len +
		    2 + strnlen(tinfo->cwd, SCAP_MAX_PATH_SIZE) +
		    sizeof(uint64_t) +	// fdlimit
		    sizeof(uint32_t) +	// uid
		    sizeof(uint32_t) +	// gid
		    sizeof(uint32_t);
	}

	//
	// Create the block
	//
	bh.block_type = PL_BLOCK_TYPE;
	bh.block_total_length = scap_normalize_block_len(sizeof(block_header) + totlen + 4);

	if(fwrite(&bh, sizeof(bh), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (1)");
		return SCAP_FAILURE;
	}

	//
	// Second pass pass of the table to dump it
	//
	HASH_ITER(hh, handle->m_proclist, tinfo, ttinfo)
	{
		commlen = strnlen(tinfo->comm, SCAP_MAX_PATH_SIZE);
		exelen = strnlen(tinfo->exe, SCAP_MAX_PATH_SIZE);
		argslen = tinfo->args_len;
		cwdlen = strnlen(tinfo->cwd, SCAP_MAX_PATH_SIZE);

		if(fwrite(&(tinfo->tid), sizeof(uint64_t), 1, f) != 1 ||
		        fwrite(&(tinfo->pid), sizeof(uint64_t), 1, f) != 1 ||
		        fwrite(&(tinfo->ppid), sizeof(uint64_t), 1, f) != 1 ||
		        fwrite(&commlen,  sizeof(uint16_t), 1, f) != 1 ||
		        fwrite(tinfo->comm, 1,  commlen, f) != commlen ||
		        fwrite(&exelen,  sizeof(uint16_t), 1, f) != 1 ||
		        fwrite(tinfo->exe, 1, exelen, f) != exelen ||
		        fwrite(&argslen,  sizeof(uint16_t), 1, f) != 1 ||
		        fwrite(tinfo->args, 1, argslen, f) != argslen ||
		        fwrite(&cwdlen,  sizeof(uint16_t), 1, f) != 1 ||
		        fwrite(tinfo->cwd, 1, cwdlen, f) != cwdlen ||
		        fwrite(&(tinfo->fdlimit), sizeof(uint64_t), 1, f) != 1 ||
		        fwrite(&(tinfo->flags), sizeof(uint32_t), 1, f) != 1 ||
		        fwrite(&(tinfo->uid), sizeof(uint32_t), 1, f) != 1 ||
		        fwrite(&(tinfo->gid), sizeof(uint32_t), 1, f) != 1)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (2)");
			return SCAP_FAILURE;
		}
	}

	//
	// Blocks need to be 4-byte padded
	//
	if(scap_write_padding(f, totlen) != SCAP_SUCCESS)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (3)");
		return SCAP_FAILURE;
	}

	//
	// Create the trailer
	//
	bt = bh.block_total_length;
	if(fwrite(&bt, sizeof(bt), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (4)");
		return SCAP_FAILURE;
	}

	return SCAP_SUCCESS;
}

//
// Write the machine info block
//
#ifdef _WIN32
int32_t scap_write_machine_info(scap_t *handle, FILE *f)
{
	ASSERT(false);
	snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "scap_write_machine_info not implemented under Windows");
	return SCAP_FAILURE;
}
#else
int32_t scap_write_machine_info(scap_t *handle, FILE *f)
{
	block_header bh;
	uint32_t bt;

	//
	// Write the section header
	//
	bh.block_type = MI_BLOCK_TYPE;
	bh.block_total_length = scap_normalize_block_len(sizeof(block_header) + sizeof(scap_machine_info) + 4);

	bt = bh.block_total_length;

	if(fwrite(&bh, sizeof(bh), 1, f) != 1 ||
	        fwrite(&handle->m_machine_info, sizeof(handle->m_machine_info), 1, f) != 1 ||
	        fwrite(&bt, sizeof(bt), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (MI1)");
		return SCAP_FAILURE;
	}

	return SCAP_SUCCESS;
}
#endif // _WIN32

//
// Write the interface list block
//
#ifdef _WIN32
int32_t scap_write_iflist(scap_t *handle, FILE *f)
{
	ASSERT(false);
	snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "scap_write_iflist not implemented under Windows");
	return SCAP_FAILURE;
}
#else
int32_t scap_write_iflist(scap_t *handle, FILE *f)
{
	block_header bh;
	uint32_t bt;
	uint32_t entrylen;
	uint32_t j;

	//
	// Get the interface list
	//
	if(handle->m_addrlist == NULL)
	{
		ASSERT(false);
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to trace file: interface list missing");
		return SCAP_FAILURE;
	}

	//
	// Create the block
	//
	bh.block_type = IL_BLOCK_TYPE;
	bh.block_total_length = scap_normalize_block_len(sizeof(block_header) + handle->m_addrlist->totlen + 4);

	if(fwrite(&bh, sizeof(bh), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (IF1)");
		return SCAP_FAILURE;
	}

	//
	// Dump the ipv4 list
	//
	for(j = 0; j < handle->m_addrlist->n_v4_addrs; j++)
	{
		scap_ifinfo_ipv4 *entry = &(handle->m_addrlist->v4list[j]);

		entrylen = sizeof(scap_ifinfo_ipv4) + entry->ifnamelen - SCAP_MAX_PATH_SIZE;

		if(fwrite(entry, entrylen, 1, f) != 1)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (IF2)");
			return SCAP_FAILURE;
		}
	}

	//
	// Dump the ipv6 list
	//
	for(j = 0; j < handle->m_addrlist->n_v6_addrs; j++)
	{
		scap_ifinfo_ipv6 *entry = &(handle->m_addrlist->v6list[j]);

		entrylen = sizeof(scap_ifinfo_ipv6) + entry->ifnamelen - SCAP_MAX_PATH_SIZE;

		if(fwrite(entry, entrylen, 1, f) != 1)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (IF2)");
			return SCAP_FAILURE;
		}
	}

	//
	// Blocks need to be 4-byte padded
	//
	if(scap_write_padding(f, handle->m_addrlist->totlen) != SCAP_SUCCESS)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (IF3)");
		return SCAP_FAILURE;
	}

	//
	// Create the trailer
	//
	bt = bh.block_total_length;
	if(fwrite(&bt, sizeof(bt), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (IF4)");
		return SCAP_FAILURE;
	}

	return SCAP_SUCCESS;
}
#endif // _WIN32

//
// Create the dump file headers and add the tables
//
static scap_dumper_t *scap_setup_dump(scap_t *handle, FILE *f, const char *fname)
{
	block_header bh;
	section_header_block sh;
	uint32_t bt;

	//
	// Write the section header
	//
	bh.block_type = SHB_BLOCK_TYPE;
	bh.block_total_length = sizeof(block_header) + sizeof(section_header_block) + 4;

	sh.byte_order_magic = SHB_MAGIC;
	sh.major_version = CURRENT_MAJOR_VERSION;
	sh.minor_version = CURRENT_MINOR_VERSION;
	sh.section_length = 0xffffffffffffffffLL;

	bt = bh.block_total_length;

	if(fwrite(&bh, sizeof(bh), 1, f) != 1 ||
	        fwrite(&sh, sizeof(sh), 1, f) != 1 ||
	        fwrite(&bt, sizeof(bt), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file %s  (5)", fname);
		return NULL;
	}

	//
	// Write the machine info
	//
	if(scap_write_machine_info(handle, f) != SCAP_SUCCESS)
	{
		return NULL;
	}

	//
	// Write the interface list
	//
	if(scap_write_iflist(handle, f) != SCAP_SUCCESS)
	{
		return NULL;
	}

	//
	// Write the process list
	//
	if(scap_write_proclist(handle, f) != SCAP_SUCCESS)
	{
		return NULL;
	}

	//
	// Write the fd lists
	//

	if(scap_write_fdlist(handle, f) != SCAP_SUCCESS)
	{
		return NULL;
	}

	//
	// Done, return the file
	//
	return (scap_dumper_t *)f;
}

//
// Open a "savefile" for writing.
//
scap_dumper_t *scap_dump_open(scap_t *handle, const char *fname)
{
	FILE *f;

	if(fname[0] == '-' && fname[1] == '\0')
	{
		f = stdout;
		fname = "standard output";
	}
	else
	{
		f = fopen(fname, "wb");

		if(f == NULL)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "can't open %s", fname);
			return NULL;
		}
	}

	return scap_setup_dump(handle, f, fname);
}

//
// Close a "savefile" opened with scap_dump_open
//
void scap_dump_close(scap_dumper_t *d)
{
	fclose((FILE *)d);
}

//
// Write an event to a dump file
//
int32_t scap_dump(scap_t *handle, scap_dumper_t *d, scap_evt *event, uint16_t cpuid)
{
	block_header bh;
	uint32_t bt;
	FILE *f = (FILE *)d;

	//
	// Write the section header
	//
	bh.block_type = EV_BLOCK_TYPE;
	bh.block_total_length = scap_normalize_block_len(sizeof(block_header) + sizeof(cpuid) + event->len + 4);
	bt = bh.block_total_length;

	if(fwrite(&bh, sizeof(bh), 1, f) != 1 ||
	        fwrite(&cpuid, sizeof(cpuid), 1, f) != 1 ||
	        fwrite(event, event->len, 1, f) != 1 ||
	        scap_write_padding(f, sizeof(cpuid) + event->len) != SCAP_SUCCESS ||
	        fwrite(&bt, sizeof(bt), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error writing to file (6)");
		return SCAP_FAILURE;
	}

	return SCAP_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// READ FUNCTIONS
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

//
// Load the machine info block
//
int32_t scap_read_machine_info(scap_t *handle, FILE *f, uint32_t block_length)
{
	//
	// Read the section header block
	//
	if(fread(&handle->m_machine_info, sizeof(handle->m_machine_info), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error reading from file (1)");
		return SCAP_FAILURE;
	}

	return SCAP_SUCCESS;
}

//
// Parse a process list block
//
int32_t scap_read_proclist(scap_t *handle, FILE *f, uint32_t block_length)
{
	size_t readsize;
	size_t totreadsize = 0;
	struct scap_threadinfo tinfo;
	uint16_t stlen;
	uint32_t padding;
	int32_t padding_len;
	int32_t uth_status = SCAP_SUCCESS;
	struct scap_threadinfo *ntinfo;

	tinfo.fdlist = NULL;
	tinfo.flags = 0;

	while(((int32_t)block_length - (int32_t)totreadsize) >= 4)
	{
		//
		// tid
		//
		readsize = fread(&(tinfo.tid), 1, sizeof(uint64_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint64_t));

		totreadsize += readsize;

		//
		// pid
		//
		readsize = fread(&(tinfo.pid), 1, sizeof(uint64_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint64_t));

		totreadsize += readsize;

		//
		// ppid
		//
		readsize = fread(&(tinfo.ppid), 1, sizeof(uint64_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint64_t));

		totreadsize += readsize;

		//
		// comm
		//
		readsize = fread(&(stlen), 1, sizeof(uint16_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint16_t));

		if(stlen >= SCAP_MAX_PATH_SIZE)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "invalid commlen %d", stlen);
			return SCAP_FAILURE;
		}

		totreadsize += readsize;

		readsize = fread(tinfo.comm, 1, stlen, f);
		CHECK_READ_SIZE(readsize, stlen);

		// the string is not null-terminated on file
		tinfo.comm[stlen] = 0;

		totreadsize += readsize;

		//
		// exe
		//
		readsize = fread(&(stlen), 1, sizeof(uint16_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint16_t));

		if(stlen >= SCAP_MAX_PATH_SIZE)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "invalid exelen %d", stlen);
			return SCAP_FAILURE;
		}

		totreadsize += readsize;

		readsize = fread(tinfo.exe, 1, stlen, f);
		CHECK_READ_SIZE(readsize, stlen);

		// the string is not null-terminated on file
		tinfo.exe[stlen] = 0;

		totreadsize += readsize;

		//
		// args
		//
		readsize = fread(&(stlen), 1, sizeof(uint16_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint16_t));

		if(stlen >= SCAP_MAX_PATH_SIZE)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "invalid argslen %d", stlen);
			return SCAP_FAILURE;
		}

		totreadsize += readsize;

		readsize = fread(tinfo.args, 1, stlen, f);
		CHECK_READ_SIZE(readsize, stlen);

		// the string is not null-terminated on file
		tinfo.args[stlen] = 0;
		tinfo.args_len = stlen;

		totreadsize += readsize;

		//
		// cwd
		//
		readsize = fread(&(stlen), 1, sizeof(uint16_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint16_t));

		if(stlen >= SCAP_MAX_PATH_SIZE)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "invalid cwdlen %d", stlen);
			return SCAP_FAILURE;
		}

		totreadsize += readsize;

		readsize = fread(tinfo.cwd, 1, stlen, f);
		CHECK_READ_SIZE(readsize, stlen);

		// the string is not null-terminated on file
		tinfo.cwd[stlen] = 0;

		totreadsize += readsize;

		//
		// fdlimit
		//
		readsize = fread(&(tinfo.fdlimit), 1, sizeof(uint64_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint64_t));

		totreadsize += readsize;

		//
		// flags
		//
		readsize = fread(&(tinfo.flags), 1, sizeof(uint32_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint32_t));

		totreadsize += readsize;

		//
		// uid
		//
		readsize = fread(&(tinfo.uid), 1, sizeof(uint32_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint32_t));

		totreadsize += readsize;

		//
		// gid
		//
		readsize = fread(&(tinfo.gid), 1, sizeof(uint32_t), f);
		CHECK_READ_SIZE(readsize, sizeof(uint32_t));

		totreadsize += readsize;

		//
		// All parsed. Allocate the new entry and copy the temp one into into it.
		//
		ntinfo = (scap_threadinfo *)malloc(sizeof(scap_threadinfo));
		if(ntinfo == NULL)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "process table allocation error (fd1)");
			return SCAP_FAILURE;
		}

		// Structure copy
		*ntinfo = tinfo;

		//
		// All parsed. Add the entry to the table
		//
		HASH_ADD_INT64(handle->m_proclist, tid, ntinfo);
		if(uth_status != SCAP_SUCCESS)
		{
			free(ntinfo);
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "process table allocation error (fd2)");
			return SCAP_FAILURE;
		}
	}

	//
	// Read the padding bytes so we properly align to the end of the data
	//
	padding_len = ((int32_t)block_length - (int32_t)totreadsize);
	ASSERT(padding_len >= 0);

	readsize = fread(&padding, 1, padding_len, f);
	CHECK_READ_SIZE(readsize, padding_len);

	return SCAP_SUCCESS;
}

//
// Parse an interface list block
//
int32_t scap_read_iflist(scap_t *handle, FILE *f, uint32_t block_length)
{
	int32_t res = SCAP_SUCCESS;
	size_t readsize;
	size_t totreadsize;
	char *readbuf = NULL;
	char *pif;
	uint16_t iftype;
	uint16_t ifnamlen;
	uint32_t toread;
	uint32_t entrysize;
	uint32_t ifcnt4 = 0;
	uint32_t ifcnt6 = 0;

	//
	// If the list of interfaces was already allocated for this handle (for example because this is
	// not the first interface list block), free it
	//
	if(handle->m_addrlist != NULL)
	{
		scap_free_iflist(handle->m_addrlist);
		handle->m_addrlist = NULL;
	}

	//
	// Bring the block to memory
	// We assume that this block is always small enough that we can read it in a single shot
	//
	readbuf = (char *)malloc(block_length);
	if(!readbuf)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "memory allocation error in scap_read_iflist");
		return SCAP_FAILURE;
	}

	readsize = fread(readbuf, 1, block_length, f);
	CHECK_READ_SIZE(readsize, block_length);

	//
	// First pass, count the number of addresses
	//
	pif = readbuf;
	totreadsize = 0;

	while(true)
	{
		toread = (int32_t)block_length - (int32_t)totreadsize;

		if(toread < 4)
		{
			break;
		}

		iftype = *(uint16_t *)pif;
		ifnamlen = *(uint16_t *)(pif + 2);

		if(iftype == SCAP_FD_IPV4_SOCK)
		{
			entrysize = sizeof(scap_ifinfo_ipv4) + ifnamlen - SCAP_MAX_PATH_SIZE;

			if(toread < entrysize)
			{
				snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "trace file has corrupted interface list(1)");
				res = SCAP_FAILURE;
				goto scap_read_iflist_error;
			}

			pif += entrysize;
			totreadsize += entrysize;

			ifcnt4++;
		}
		else if(iftype == SCAP_FD_IPV6_SOCK)
		{
			entrysize = sizeof(scap_ifinfo_ipv6) + ifnamlen - SCAP_MAX_PATH_SIZE;

			if(toread < entrysize)
			{
				snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "trace file has corrupted interface list(1)");
				res = SCAP_FAILURE;
				goto scap_read_iflist_error;
			}

			pif += entrysize;
			totreadsize += entrysize;

			ifcnt6++;
		}
		else
		{
			ASSERT(false);
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "unknown interface type %d", (int)iftype);
			res = SCAP_FAILURE;
			goto scap_read_iflist_error;
		}
	}

	//
	// Allocate the handle and the arrays
	//
	handle->m_addrlist = (scap_addrlist *)malloc(sizeof(scap_addrlist));
	if(!handle->m_addrlist)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "scap_read_iflist allocation failed(1)");
		res = SCAP_FAILURE;
		goto scap_read_iflist_error;
	}

	handle->m_addrlist->n_v4_addrs = 0;
	handle->m_addrlist->n_v6_addrs = 0;
	handle->m_addrlist->v4list = NULL;
	handle->m_addrlist->v6list = NULL;
	handle->m_addrlist->totlen = block_length;

	if(ifcnt4 != 0)
	{
		handle->m_addrlist->v4list = (scap_ifinfo_ipv4 *)malloc(ifcnt4 * sizeof(scap_ifinfo_ipv4));
		if(!handle->m_addrlist->v4list)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "scap_read_iflist allocation failed(2)");
			res = SCAP_FAILURE;
			goto scap_read_iflist_error;
		}
	}
	else
	{
		handle->m_addrlist->v4list = NULL;
	}

	if(ifcnt6 != 0)
	{
		handle->m_addrlist->v6list = (scap_ifinfo_ipv6 *)malloc(ifcnt6 * sizeof(scap_ifinfo_ipv6));
		if(!handle->m_addrlist->v6list)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "getifaddrs allocation failed(3)");
			res = SCAP_FAILURE;
			goto scap_read_iflist_error;
		}
	}
	else
	{
		handle->m_addrlist->v6list = NULL;
	}

	handle->m_addrlist->n_v4_addrs = ifcnt4;
	handle->m_addrlist->n_v6_addrs = ifcnt6;

	//
	// Second pass: populate the arrays
	//
	ifcnt4 = 0;
	ifcnt6 = 0;
	pif = readbuf;
	totreadsize = 0;

	while(true)
	{
		toread = (int32_t)block_length - (int32_t)totreadsize;

		if(toread < 4)
		{
			break;
		}

		iftype = *(uint16_t *)pif;
		ifnamlen = *(uint16_t *)(pif + 2);

		if(ifnamlen >= SCAP_MAX_PATH_SIZE)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "trace file has corrupted interface list(0)");
			res = SCAP_FAILURE;
			goto scap_read_iflist_error;
		}

		if(iftype == SCAP_FD_IPV4_SOCK)
		{
			entrysize = sizeof(scap_ifinfo_ipv4) + ifnamlen - SCAP_MAX_PATH_SIZE;

			if(toread < entrysize)
			{
				snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "trace file has corrupted interface list(1)");
				res = SCAP_FAILURE;
				goto scap_read_iflist_error;
			}

			// Copy the entry
			memcpy(handle->m_addrlist->v4list + ifcnt4, pif, entrysize);

			// Make sure the name string is NULL-terminated
			*((char *)(handle->m_addrlist->v4list + ifcnt4) + entrysize) = 0;

			pif += entrysize;
			totreadsize += entrysize;

			ifcnt4++;
		}
		else if(iftype == SCAP_FD_IPV6_SOCK)
		{
			entrysize = sizeof(scap_ifinfo_ipv6) + ifnamlen - SCAP_MAX_PATH_SIZE;

			if(toread < entrysize)
			{
				snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "trace file has corrupted interface list(1)");
				res = SCAP_FAILURE;
				goto scap_read_iflist_error;
			}

			// Copy the entry
			memcpy(handle->m_addrlist->v6list + ifcnt6, pif, entrysize);

			// Make sure the name string is NULL-terminated
			*((char *)(handle->m_addrlist->v6list + ifcnt6) + entrysize) = 0;

			pif += entrysize;
			totreadsize += entrysize;

			ifcnt6++;
		}
		else
		{
			ASSERT(false);
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "unknown interface type %d", (int)iftype);
			res = SCAP_FAILURE;
			goto scap_read_iflist_error;
		}
	}

	//
	// Release the read storage
	//
	free(readbuf);

	return res;

scap_read_iflist_error:
	scap_free_iflist(handle->m_addrlist);

	if(readbuf)
	{
		free(readbuf);
	}

	return res;
}

//
// Parse a process list block
//
int32_t scap_read_fdlist(scap_t *handle, FILE *f, uint32_t block_length)
{
	size_t readsize;
	size_t totreadsize = 0;
	struct scap_threadinfo *tinfo;
	scap_fdinfo fdi;
	scap_fdinfo *nfdi;
	//  uint16_t stlen;
	uint64_t tid;
	int32_t uth_status = SCAP_SUCCESS;
	uint32_t padding;
	int32_t padding_len;

	//
	// Read the tid
	//
	readsize = fread(&tid, 1, sizeof(tid), f);
	CHECK_READ_SIZE(readsize, sizeof(tid));
	totreadsize += readsize;

	//
	// Identify the process descriptor
	//
	HASH_FIND_INT64(handle->m_proclist, &tid, tinfo);
	if(tinfo == NULL)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "corrupted trace file. FD block references TID %"PRIu64", which doesn't exist.",
		         tid);
		return SCAP_FAILURE;
	}

	while(((int32_t)block_length - (int32_t)totreadsize) >= 4)
	{
		if(scap_fd_read_from_disk(handle, &fdi, &readsize, f) != SCAP_SUCCESS)
		{
			return SCAP_FAILURE;
		}
		totreadsize += readsize;

		//
		// Parsed successfully. Allocate the new entry and copy the temp one into into it.
		//
		nfdi = (scap_fdinfo *)malloc(sizeof(scap_fdinfo));
		if(nfdi == NULL)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "process table allocation error (fd1)");
			return SCAP_FAILURE;
		}

		// Structure copy
		*nfdi = fdi;

		//
		// Add the entry to the table
		//
		HASH_ADD_INT64(tinfo->fdlist, fd, nfdi);
		if(uth_status != SCAP_SUCCESS)
		{
			free(nfdi);
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "process table allocation error (fd2)");
			return SCAP_FAILURE;
		}
	}

	//
	// Read the padding bytes so we properly align to the end of the data
	//
	padding_len = ((int32_t)block_length - (int32_t)totreadsize);
	ASSERT(padding_len >= 0);

	readsize = fread(&padding, 1, padding_len, f);
	CHECK_READ_SIZE(readsize, padding_len);

	return SCAP_SUCCESS;
}

//
// Parse the headers of a trace file and load the tables
//
int32_t scap_read_init(scap_t *handle, FILE *f)
{
	block_header bh;
	section_header_block sh;
	uint32_t bt;
	size_t readsize;
	size_t toread;
	int fseekres;

	//
	// Read the section header block
	//
	if(fread(&bh, sizeof(bh), 1, f) != 1 ||
	        fread(&sh, sizeof(sh), 1, f) != 1 ||
	        fread(&bt, sizeof(bt), 1, f) != 1)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error reading from file (1)");
		return SCAP_FAILURE;
	}

	if(bh.block_type != SHB_BLOCK_TYPE)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "invalid block type");
		return SCAP_FAILURE;
	}

	if(sh.byte_order_magic != 0x1a2b3c4d)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "invalid magic number");
		return SCAP_FAILURE;
	}

	//
	// Read the metadata blocks (processes, FDs, etc.)
	//
	while(true)
	{
		readsize = fread(&bh, 1, sizeof(bh), f);
		CHECK_READ_SIZE(readsize, sizeof(bh));

		switch(bh.block_type)
		{
		case MI_BLOCK_TYPE:
			if(scap_read_machine_info(handle, f, bh.block_total_length - sizeof(block_header) - 4) != SCAP_SUCCESS)
			{
				return SCAP_FAILURE;
			}
			break;
		case PL_BLOCK_TYPE:
			if(scap_read_proclist(handle, f, bh.block_total_length - sizeof(block_header) - 4) != SCAP_SUCCESS)
			{
				return SCAP_FAILURE;
			}
			break;
		case FDL_BLOCK_TYPE:
			if(scap_read_fdlist(handle, f, bh.block_total_length - sizeof(block_header) - 4) != SCAP_SUCCESS)
			{
				return SCAP_FAILURE;
			}
			break;
		case EV_BLOCK_TYPE:
			//
			// We're done with the metadata headers. Rewind the file position so we are aligned to start reading the events.
			//
			fseekres = fseek(f, (long)0 - sizeof(bh), SEEK_CUR);
			if(fseekres == 0)
			{
				return SCAP_SUCCESS;
			}
			else
			{
				snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "error seeking in file");
				return SCAP_FAILURE;
			}
		case IL_BLOCK_TYPE:
			if(scap_read_iflist(handle, f, bh.block_total_length - sizeof(block_header) - 4) != SCAP_SUCCESS)
			{
				return SCAP_FAILURE;
			}
			break;
		default:
			//
			// Unknwon block type. Skip the block.
			//
			toread = bh.block_total_length - sizeof(block_header) - 4;
			fseekres = fseek(f, toread, SEEK_CUR);
			if(fseekres != 0)
			{
				snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "corrupted input file. Can't skip block of type %x and size %u.",
				         (int)bh.block_type,
				         (unsigned int)toread);
				return SCAP_FAILURE;
			}
			break;
		}

		//
		// Read and validate the trailer
		//
		readsize = fread(&bt, 1, sizeof(bt), f);
		CHECK_READ_SIZE(readsize, sizeof(bt));

		if(bt != bh.block_total_length)
		{
			snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "wrong block total length, header=%u, trailer=%u",
			         bh.block_total_length,
			         bt);
			return SCAP_FAILURE;
		}
	}

	return SCAP_SUCCESS;
}

//
// Read an event from disk
//
int32_t scap_next_offline(scap_t *handle, OUT scap_evt **pevent, OUT uint16_t *pcpuid)
{
	block_header bh;
	size_t readsize;
	uint32_t readlen;
	FILE *f = handle->m_file;

	ASSERT(f != NULL);

	//
	// Read the block header
	//
	readsize = fread(&bh, 1, sizeof(bh), f);
	if(readsize != sizeof(bh))
	{
		if(readsize == 0)
		{
			//
			// We read exactly 0 bytes. This indicates a correct end of file.
			//
			return SCAP_EOF;
		}
		else
		{
			CHECK_READ_SIZE(readsize, sizeof(bh));
		}
	}

	if(bh.block_type != EV_BLOCK_TYPE)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "unexpected block type %u", (uint32_t)bh.block_type);
		return SCAP_FAILURE;
	}

	if(bh.block_total_length < sizeof(bh) + sizeof(struct ppm_evt_hdr) + 4)
	{
		snprintf(handle->m_lasterr, SCAP_LASTERR_SIZE, "block length too short %u", (uint32_t)bh.block_total_length);
		return SCAP_FAILURE;
	}

	//
	// Read the event
	//
	readlen = bh.block_total_length - sizeof(bh);
	readsize = fread(handle->m_file_evt_buf, 1, readlen, f);
	CHECK_READ_SIZE(readsize, readlen);

	*pcpuid = *(uint16_t *)handle->m_file_evt_buf;
	*pevent = (struct ppm_evt_hdr *)(handle->m_file_evt_buf + sizeof(uint16_t));
	return SCAP_SUCCESS;
}
