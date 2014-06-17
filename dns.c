/*
 * Copyright (c) 2014 Mike Belopuhov
 * Copyright (c) 2014 Eric Faurot <eric@faurot.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <event.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <asr.h>

#include "icb.h"
#include "icbd.h"

void dns_done_host(struct asr_result *, void *);
void dns_done_reverse(struct asr_result *, void *);

extern int dodns;

void
dns_done_host(struct asr_result *ar, void *arg)
{
	struct icb_session *is = arg;

	if (ar->ar_addrinfo)
		freeaddrinfo(ar->ar_addrinfo);

	/* just check that there's no error */
	if (ar->ar_gai_errno == 0) {
		if (strncmp(is->hostname, "localhost",
		    sizeof "localhost" - 1) == 0)
			strlcpy(is->host, "unknown", ICB_MAXHOSTLEN);
		else if (strlen(is->hostname) < ICB_MAXHOSTLEN)
			strlcpy(is->host, is->hostname, ICB_MAXHOSTLEN);
	} else
		icbd_log(is, LOG_WARNING, "dns resolution failed: %s",
		    gai_strerror(ar->ar_gai_errno));

	if (ISSETF(is->flags, ICB_SF_PENDINGDROP)) {
		free(is);
		return;
	}

	CLRF(is->flags, ICB_SF_DNSINPROGRESS);
}

void
dns_done_reverse(struct asr_result *ar, void *arg)
{
	struct icb_session *is = arg;
	struct asr_query *as;
	struct addrinfo	hints;

	if (ISSETF(is->flags, ICB_SF_PENDINGDROP)) {
		free(is);
		return;
	}

	if (ar->ar_gai_errno == 0) {
		icbd_log(is, LOG_DEBUG, "reverse dns resolved %s to %s",
		    is->host, is->hostname);
		/* try to verify that it resolves back */
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
		as = getaddrinfo_async(is->hostname, NULL, &hints, NULL);
		event_asr_run(as, dns_done_host, is);
	} else {
		icbd_log(is, LOG_WARNING, "reverse dns resolution failed: %s",
		    gai_strerror(ar->ar_gai_errno));
		CLRF(is->flags, ICB_SF_DNSINPROGRESS);
	}
}

void
dns_resolve(struct icb_session *is, struct sockaddr *sa)
{
	struct asr_query *as;

	if (!dodns)
		return;

	SETF(is->flags, ICB_SF_DNSINPROGRESS);

	if (verbose)
		icbd_log(is, LOG_DEBUG, "resolving: %s", is->host);

	as = getnameinfo_async(sa, sa->sa_len, is->hostname,
	    sizeof is->hostname, NULL, 0, NI_NOFQDN, NULL);
	event_asr_run(as, dns_done_reverse, is);
}

