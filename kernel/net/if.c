/* =============================================================================
 * XIU Operating System — Network Interface Registry & Dispatch
 * kernel/net/if.c
 * ============================================================================= */

#include <net/if.h>
#include <net/protocols.h>
#include <kernel/spinlock.h>
#include <kernel/panic.h>

static ifnet_t     *s_if_list = nullptr;
static spinlock_t   s_if_lock;

void if_init(void) {
    spinlock_init(&s_if_lock);
    s_if_list = nullptr;
}

xiu_error_t if_attach(ifnet_t *ifp) {
    if (!ifp) return XIU_ERR_INVALID;

    spinlock_init(&ifp->if_lock);
    irq_flags_t flags = spinlock_lock_irqsave(&s_if_lock);
    ifp->if_next = s_if_list;
    s_if_list = ifp;
    spinlock_unlock_irqrestore(&s_if_lock, flags);

    kprintf("[net] Interface attached: %s (flags=0x%04x, mtu=%u)\n",
            ifp->if_name, ifp->if_flags, ifp->if_mtu);
    return XIU_SUCCESS;
}

void if_detach(ifnet_t *ifp) {
    if (!ifp) return;

    irq_flags_t flags = spinlock_lock_irqsave(&s_if_lock);
    ifnet_t **curr = &s_if_list;
    while (*curr) {
        if (*curr == ifp) {
            *curr = ifp->if_next;
            break;
        }
        curr = &(*curr)->if_next;
    }
    spinlock_unlock_irqrestore(&s_if_lock, flags);
}

ifnet_t *if_lookup(const char *name) {
    if (!name) return nullptr;

    irq_flags_t flags = spinlock_lock_irqsave(&s_if_lock);
    ifnet_t *curr = s_if_list;
    while (curr) {
        if (__builtin_strcmp(curr->if_name, name) == 0) {
            spinlock_unlock_irqrestore(&s_if_lock, flags);
            return curr;
        }
        curr = curr->if_next;
    }
    spinlock_unlock_irqrestore(&s_if_lock, flags);
    return nullptr;
}

ifnet_t *if_get_default(void) {
    irq_flags_t flags = spinlock_lock_irqsave(&s_if_lock);
    ifnet_t *curr = s_if_list;
    while (curr) {
        // prefer external ethernet interface over loopback
        if (!(curr->if_flags & IFF_LOOPBACK) && (curr->if_flags & IFF_UP)) {
            spinlock_unlock_irqrestore(&s_if_lock, flags);
            return curr;
        }
        curr = curr->if_next;
    }
    curr = s_if_list;
    spinlock_unlock_irqrestore(&s_if_lock, flags);
    return curr;
}

ifnet_t *if_get_list(void) {
    return s_if_list;
}

extern void ethernet_input(ifnet_t *ifp, mbuf_t *m);
extern void ip_input(ifnet_t *ifp, mbuf_t *m);

void if_input(ifnet_t *ifp, mbuf_t *m) {
    if (!ifp || !m) return;

    if (ifp->if_flags & IFF_LOOPBACK) {
        ip_input(ifp, m);
    } else {
        ethernet_input(ifp, m);
    }
}
