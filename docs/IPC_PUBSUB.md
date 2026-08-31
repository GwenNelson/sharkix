# SHARKIX IPC META ENDPOINTS AND PUB/SUB

## IPC MESSAGE MODEL

Every IPC message carries:

```text
sender ID
word 0
word 1
word 2
word 3
word 4
```

The sender ID is separate from the five message words.

The sender ID always identifies the original sender of the message.

This remains true regardless of how the message is eventually received.

For example:

```text
Process A sends to endpoint X
X generates a readiness notification to meta endpoint M
Process B receives the notification from M

sender ID = A
```

Likewise:

```text
Process A publishes to publisher endpoint P
P distributes the publication to subscriber endpoint S
Process B receives the publication from S

sender ID = A
```

The kernel does not replace the sender ID with:

```text
the kernel
the publisher endpoint
the subscriber endpoint
the meta endpoint
the process which owns the endpoint
```

The sender ID is always propagated from the original userspace sender.

For kernel-originated events which do not derive from an original userspace message, a reserved kernel sender ID may be used.

The sender ID is not part of the five-word payload and does not consume any message word.


## ENDPOINT TYPES

All IPC queues are represented internally by `ipc_endpoint_t`.

An endpoint has a type which determines who may write to it and how messages sent to it are interpreted.

```c
typedef enum ipc_endpoint_type {
    IPC_ENDPOINT_NORMAL,
    IPC_ENDPOINT_META,
    IPC_ENDPOINT_SUBSCRIBER,
    IPC_ENDPOINT_PUBLISHER
} ipc_endpoint_type_t;
```

### IPC_ENDPOINT_NORMAL

An ordinary IPC FIFO.

Processes with appropriate authority may send messages to it.

Processes with appropriate authority may receive messages from it.

All five message words are sender-controlled.


### IPC_ENDPOINT_META

A kernel-controlled IPC FIFO used for readiness notifications.

Userspace may receive from it but may never directly send to it.

Only the kernel may enqueue messages onto it.

A meta message identifies another endpoint or notifyable object which has become ready.

The original sender ID associated with the event is preserved.


### IPC_ENDPOINT_SUBSCRIBER

An endpoint registered as a subscriber to a publisher endpoint.

Userspace may receive from it but may not directly send to it.

Only the kernel may enqueue publication messages onto it.

Word 0 is reserved for kernel-controlled pub/sub status information.

Words 1 through 4 contain publisher-controlled payload data.

The sender ID is the original publisher's sender ID.


### IPC_ENDPOINT_PUBLISHER

A pub/sub publication endpoint.

Processes explicitly authorised by the owner may publish to it.

Sending to a publisher endpoint does not enqueue an ordinary message for later receive from the publisher endpoint itself.

Instead, the kernel distributes the publication to the publisher endpoint's registered subscriber endpoints.

Publication data consists of four publisher-controlled words because word 0 is reserved in subscriber messages.


## MESSAGE WORD SEMANTICS

Normal IPC messages expose all five words:

```text
word 0    sender-controlled
word 1    sender-controlled
word 2    sender-controlled
word 3    sender-controlled
word 4    sender-controlled
```

Pub/sub messages reserve word 0:

```text
word 0    kernel-controlled
word 1    publication data
word 2    publication data
word 3    publication data
word 4    publication data
```

Therefore a publisher may supply at most four message words per publication.

The kernel constructs the subscriber message from:

```text
sender ID    original publisher sender ID
word 0       kernel pub/sub metadata
word 1-4     publisher supplied data
```

Userspace must never be able to spoof word 0 on a subscriber endpoint.


## NON-BLOCKING RECEIVE

IPC provides both blocking and non-blocking receive.

Conceptually:

```c
SYS_IPC_RECV(endpoint, ...);
SYS_IPC_TRY_RECV(endpoint, ...);
```

`SYS_IPC_RECV` blocks until a message is available.

`SYS_IPC_TRY_RECV` returns immediately.

If no message is available:

```text
IPC_EMPTY
```

is returned.

The returned message still contains:

```text
sender ID
five message words
```

subject to the semantics of the endpoint type.

Non-blocking receive is required for efficiently draining endpoints after receiving readiness notifications from a meta endpoint.


# META ENDPOINTS

A meta endpoint is simply an `ipc_endpoint_t` whose type is:

```text
IPC_ENDPOINT_META
```

Its queue, locking, receive behaviour and scheduler interaction are otherwise the same as ordinary IPC endpoints.

Only the kernel may enqueue messages onto it.


## NOTIFICATION TARGET

A notifyable endpoint may nominate a meta endpoint as its notification target.

Conceptually:

```c
ipc_endpoint_t *notify_endpoint;
bool notification_pending;
```

`notify_endpoint` is either NULL or refers to an `IPC_ENDPOINT_META` endpoint.


## META NOTIFICATION CONTENT

When an endpoint becomes interesting, the kernel may enqueue a notification onto its nominated meta endpoint.

The notification contains the handle of the endpoint which should be checked.

For example:

```text
Endpoint A receives a message from process X.

Meta M receives:

    sender ID = X
    payload identifies endpoint A
```

The readiness notification therefore preserves both:

```text
which endpoint became ready
who originally caused the readiness
```

The precise placement of the endpoint handle within the five words is kernel-defined because meta messages are entirely kernel-controlled.


## NOTIFICATION COALESCING

A notifyable endpoint has at most one outstanding meta notification at a time.

When notification is required:

```text
if notification_pending == false:
    set notification_pending = true
    enqueue notification

otherwise:
    do nothing
```

Therefore:

```text
A receives message 1
A receives message 2
A receives message 3
```

need only produce:

```text
Meta M:

    [ A ]
```

rather than:

```text
[ A ][ A ][ A ]
```

When the notification is consumed, the notification-pending state becomes available for a future notification.

The synchronization protocol must prevent a race where:

```text
the old notification is consumed
a new message arrives
notification_pending is cleared too late
the new readiness notification is lost
```

The exact atomic and locking mechanism is an implementation detail.

The required invariant is:

```text
If new work becomes available after the previous readiness notification
has logically been consumed, either an outstanding notification already
exists or a new notification will be generated.
```


## META NOTIFICATIONS ARE ADVISORY

A meta notification does not reserve a message.

It means only:

```text
This endpoint became ready.
```

By the time the receiver acts on it, another thread may already have consumed the available work.

Therefore this is valid:

```c
handle = SYS_IPC_RECV(meta);

if (SYS_IPC_TRY_RECV(handle, &message) == IPC_EMPTY)
    continue;
```

Stale readiness notifications are permitted.

Lost readiness notifications are not.


## TYPICAL META LOOP

```c
for (;;) {
    ipc_handle_t ready;

    SYS_IPC_RECV(meta, &ready);

    while (SYS_IPC_TRY_RECV(ready, &message) != IPC_EMPTY)
        handle_message(ready, &message);
}
```

This provides a native mechanism for listening to multiple IPC queues without requiring a separate `select()`-style syscall.


# PUB/SUB

Pub/sub is built from the same IPC endpoint implementation.

A publisher endpoint distributes publications to registered subscriber endpoints.

Subscriber queues remain ordinary FIFOs internally.

The kernel controls what may be written to them and how overflow or backpressure is handled.


## PUBLISHER ENDPOINTS

A pub/sub channel is represented by:

```text
IPC_ENDPOINT_PUBLISHER
```

The endpoint has an owner.

The owner controls which principals, processes or capabilities are authorised to publish.

Merely holding a reference to the publisher endpoint does not necessarily grant publication authority.

Conceptually:

```text
publisher owner
    |
    +-- authorise process A
    +-- authorise process B
    +-- deny process C
```

An authorised send to the publisher endpoint becomes a publication.

The publisher supplies:

```text
sender ID    automatically derived from sender
word 1
word 2
word 3
word 4
```

The publisher does not control subscriber word 0.


## SUBSCRIPTIONS

A subscription binds:

```text
publisher endpoint
subscriber endpoint
delivery policy
policy parameters
```

Conceptually:

```c
typedef struct ipc_subscription {
    ipc_endpoint_t *publisher;
    ipc_endpoint_t *subscriber;

    ipc_subscription_policy_t requested_policy;
    ipc_subscription_policy_t effective_policy;

    uint64_t timeout;
    uint64_t missed_events;

    subscription_state_t state;

    ...
} ipc_subscription_t;
```

An endpoint registered as a subscriber becomes:

```text
IPC_ENDPOINT_SUBSCRIBER
```

and may no longer receive arbitrary userspace sends.

Only the kernel may write publication messages to it.


## SUBSCRIPTION POLICY

Each subscriber requests a policy.

The publisher owner may optionally constrain or override that policy.

```c
typedef enum ipc_subscription_policy {
    IPC_SUB_RELIABLE,
    IPC_SUB_TIMEOUT,
    IPC_SUB_LOSSY
} ipc_subscription_policy_t;
```

The effective policy is determined when the subscription is established.

The subscriber may not unilaterally demand stronger delivery guarantees than the publisher owner allows.

For example, the publisher owner may:

```text
allow RELIABLE
allow TIMEOUT
allow LOSSY

or

forbid RELIABLE

or

force LOSSY

or

require TIMEOUT to fall within a permitted range
```

The exact policy-negotiation API may remain simple initially.


# PUBLICATION SEMANTICS

## PUBLICATION SERIALIZATION

Only one publication is actively being distributed by a publisher endpoint at a time.

Publications are serialized in arrival order.

If publication M1 has not yet resolved for every relevant subscriber, M2 does not begin distribution.

This preserves per-channel ordering.

No subscriber may observe:

```text
M2
M1
```

from the same publisher endpoint.


## PROGRESSIVE DELIVERY

Publication is progressive rather than transactional.

Subscribers able to accept the publication receive it immediately.

Subscribers unable to accept it are handled according to their individual policies.

The kernel begins with:

```text
pending = all subscribers
```

It then attempts delivery to every subscriber.

Conceptually:

```c
for each subscriber {
    result = ipc_try_send(subscriber_endpoint, publication);

    if (result == IPC_OK)
        resolve subscriber;
    else
        apply subscriber policy;
}
```

After the first pass:

```text
pending = only subscribers whose delivery remains unresolved
```

The publisher blocks only while:

```text
pending != 0
```

Once:

```text
pending == 0
```

publication has completed and the publishing thread is unblocked.


## PENDING COUNT

The implementation may represent unresolved deliveries using a counter.

At publication start:

```text
pending = number of subscribers participating in publication
```

Each subscriber is attempted.

Every subscriber whose delivery resolves immediately decrements pending.

After the first pass, pending therefore equals the number of unresolved subscribers.

For example:

```text
Subscribers:
    A reliable, writable
    B reliable, full
    C reliable, writable
    D lossy, full
```

Initial:

```text
pending = 4
```

Delivery:

```text
A succeeds:
    pending = 3

B full/reliable:
    pending remains 3

C succeeds:
    pending = 2

D full/lossy:
    discard oldest
    deliver new publication
    pending = 1
```

After the first pass:

```text
pending = 1
```

Only B remains unresolved.

When B eventually accepts the publication:

```text
pending = 0
```

and the publisher wakes.


## DELIVERY MUST RESOLVE EXACTLY ONCE

Each subscriber's participation in a publication must transition from unresolved to resolved exactly once.

Possible resolutions include:

```text
successfully delivered
lossy delivery completed
timeout subscriber detached
subscription destroyed or otherwise invalidated according to defined semantics
```

Two CPUs must never both resolve the same subscription and decrement the publication's pending count twice.

An internal per-delivery state may therefore be useful:

```c
typedef enum publication_delivery_state {
    PUB_DELIVERY_PENDING,
    PUB_DELIVERY_DELIVERED,
    PUB_DELIVERY_TIMED_OUT,
    PUB_DELIVERY_CANCELLED
} publication_delivery_state_t;
```

The transition away from `PUB_DELIVERY_PENDING` must be atomic or otherwise protected by appropriate locking.


# RELIABLE POLICY

A reliable subscriber must receive every publication while subscribed.

If its subscriber endpoint has space:

```text
enqueue publication
delivery resolves
```

If the endpoint is full:

```text
delivery remains unresolved
```

The publication continues to be delivered to other subscribers.

Once all immediately deliverable subscribers have been processed, the publisher blocks if any reliable subscriber remains unresolved.

When the subscriber queue becomes writable, the pending publication is retried for that subscriber.

A reliable subscriber may therefore apply indefinite backpressure.


# TIMEOUT POLICY

A timeout subscriber initially behaves exactly like a reliable subscriber.

If its queue has space:

```text
deliver normally
```

If its queue is full:

```text
delivery remains unresolved
```

The timeout begins only because this subscriber is actively preventing the current publication from completing.

Timeout does not measure:

```text
time since last receive
time since process last ran
subscription age
process inactivity
```

It specifically measures:

```text
how long this subscription has blocked completion of the current publication
```

If the subscriber becomes writable before the timeout expires:

```text
publication is delivered
delivery resolves
```

If the timeout expires first:

```text
subscription is timed out
subscription is removed from active delivery
current delivery resolves without being delivered
publication may proceed
```

If no other subscriber remains pending:

```text
pending == 0
publisher wakes
```


## TIMEOUT STATE

A timed-out subscriber endpoint remains readable so that the process can eventually discover that it lost its subscription.

The subscription enters a timed-out or detached state.

Conceptually:

```c
typedef enum subscription_state {
    IPC_SUB_ACTIVE,
    IPC_SUB_TIMED_OUT,
    IPC_SUB_DETACHED
} subscription_state_t;
```

The subscription does not silently resume reliable delivery.

Explicit re-subscription is required.


# MISSED EVENTS AND STATUS REPORTING

Each subscription contains:

```c
uint64_t missed_events;
```

This counts publications which the subscriber did not receive because of its policy state.

For a lossy subscription, it counts publications discarded from the subscriber queue.

For a timed-out subscription, it counts publications missed after the timeout caused the subscription to detach.

The count is reported through subscriber word 0.


## NORMAL PUB/SUB DELIVERY

Normal delivery:

```text
syscall status = IPC_OK

sender ID = original publisher sender
word 0    = 0
word 1-4  = publication
```


## LOSS REPORTING

If publications have been lost:

```text
syscall status = IPC_OVERRUN

sender ID = original publisher sender
word 0    = number of missed publications
word 1-4  = next available publication
```

The publication itself remains valid even when `IPC_OVERRUN` is returned.

After reporting the accumulated missed count:

```text
missed_events = 0
```


## TIMEOUT REPORTING

When a timed-out subscriber eventually catches up with messages already queued before timeout, the kernel eventually reports the timeout condition.

Conceptually:

```text
syscall status = IPC_TIMEOUT
word 0         = number of publications missed
```

If the timeout status corresponds to a publication which caused the timeout, its sender ID is preserved.

The remaining payload words may be unused for a pure timeout status.

The subscriber remains detached until explicitly re-subscribed.


# LOSSY POLICY

A lossy subscriber never blocks publication.

If the subscriber queue has room:

```text
enqueue publication
delivery resolves
```

If the subscriber queue is full:

```text
discard oldest queued publication
increment missed_events
enqueue new publication
delivery resolves
```

Therefore lossy subscribers never remain in the persistent unresolved set merely because their queue is full.

The queue always contains the newest publications which fit.


## LOSSY EXAMPLE

Queue capacity:

```text
3
```

Current queue:

```text
A
B
C
```

Publication D arrives:

```text
discard A
missed_events = 1

queue:
B
C
D
```

Publication E arrives:

```text
discard B
missed_events = 2

queue:
C
D
E
```

Subscriber receives C:

```text
status    = IPC_OVERRUN
sender ID = original sender of C
word 0    = 2
word 1-4  = payload of C
```

Then:

```text
missed_events = 0
```


# SENDER ID THROUGH PUB/SUB

Sender identity is never replaced during publication fan-out.

Suppose:

```text
Process A publishes M to publisher P.

P distributes M to:
    subscriber X
    subscriber Y
    subscriber Z
```

Then:

```text
receive X:
    sender ID = A

receive Y:
    sender ID = A

receive Z:
    sender ID = A
```

The subscribers do not see:

```text
sender ID = P
sender ID = kernel
sender ID = publisher owner
```

This allows subscribers to identify the actual original publisher without consuming any payload word.


# SUBSCRIBERS AND META ENDPOINTS

Subscriber endpoints may nominate meta endpoints exactly like normal endpoints.

Example:

```text
normal endpoint A --------\
normal endpoint B ---------\
subscriber endpoint X ------> meta M
subscriber endpoint Y -----/
```

Suppose process P publishes a message which is enqueued onto subscriber X.

X becomes readable.

The kernel may enqueue onto M:

```text
sender ID = P
message identifies X
```

Thus receiving from the meta endpoint preserves the original publication sender.

The process can then drain X:

```c
for (;;) {
    ipc_handle_t ready;
    ipc_message_t message;

    SYS_IPC_RECV(meta, &ready);

    while (SYS_IPC_TRY_RECV(ready, &message) != IPC_EMPTY)
        handle_message(ready, &message);
}
```


# PUBLISHER AUTHORISATION

Publisher endpoints are controlled objects.

The publisher owner determines which processes or capabilities may publish.

New primitive operations are required for:

```text
grant publication authority
revoke publication authority
query/test publication authority internally
```

Publication authority is distinct from ordinary endpoint ownership.

Creating or subscribing to a publisher endpoint does not automatically grant write access.


# ENDPOINT CREATION

Endpoint type must be established safely.

Normal endpoint creation produces:

```text
IPC_ENDPOINT_NORMAL
```

Meta and publisher endpoints should preferably be created directly as their intended type rather than created writable and later converted.

Subscriber endpoints may begin as normal endpoints and become controlled when successfully registered as a subscription.

The transition must ensure that no userspace send can race with conversion into `IPC_ENDPOINT_SUBSCRIBER`.


# SUBSCRIPTION REGISTRATION

A primitive equivalent to:

```c
SYS_IPC_SUBSCRIBE(
    publisher,
    subscriber_endpoint,
    requested_policy,
    policy_parameters
);
```

is required.

The operation:

```text
checks authority
checks publisher-owner policy
determines effective policy
registers the subscriber
converts the endpoint to IPC_ENDPOINT_SUBSCRIBER
initialises missed count and subscription state
```

Registration fails if the endpoint is unsuitable, already subscribed, or otherwise in use in an incompatible manner.


# UNSUBSCRIBE

An explicit unsubscribe operation is required.

It must safely handle:

```text
active publication
pending reliable delivery
pending timeout delivery
messages already queued
meta notification state
endpoint destruction
concurrent publication
```

If the subscription participates in an active publication, removing it must resolve that participation exactly once.


# META NOTIFICATION REGISTRATION

A primitive equivalent to:

```c
SYS_IPC_SET_NOTIFY(endpoint, meta_endpoint);
```

is required.

There must also be a way to:

```text
clear notification target
replace notification target
```

with appropriate ownership and authority checking.


# NEW PRIMITIVE OPERATIONS

The externally visible API requires operations corresponding to:

```text
create normal endpoint
create meta endpoint
create publisher endpoint

blocking receive
non-blocking receive

configure meta notification target

authorise publisher
revoke publisher authority

subscribe endpoint
unsubscribe endpoint

configure publisher subscription-policy restrictions
```

Exact syscall naming may follow the existing Sharkix syscall naming conventions.


# INTERNAL IPC PRIMITIVES

The kernel will benefit from internal operations equivalent to:

```c
ipc_try_send(endpoint, sender_id, message);
ipc_try_recv(endpoint, message);

ipc_kernel_enqueue(endpoint, sender_id, message);

ipc_drop_oldest(endpoint);

ipc_notify_ready(endpoint, sender_id);

ipc_publish(publisher, sender_id, publication);

ipc_subscription_try_deliver(subscription, publication);

ipc_publication_resolve(publication, subscription, reason);

ipc_publication_complete(publication);
```

These are implementation concepts and do not necessarily map one-to-one to syscalls.


# CORE ENDPOINT STRUCTURE

At minimum, `ipc_endpoint_t` requires state equivalent to:

```c
typedef struct ipc_endpoint {
    ...

    ipc_endpoint_type_t type;

    ipc_fifo_t queue;

    struct ipc_endpoint *notify_endpoint;

    bool notification_pending;

    ...

} ipc_endpoint_t;
```

The notification flag may require atomic access or another carefully defined synchronization protocol.


# PUBLISHER STATE

Publisher endpoints require additional state equivalent to:

```c
typedef struct ipc_publisher_state {
    subscription_set_t subscribers;

    ipc_publication_t *active_publication;

    authority_set_t authorised_publishers;

    publisher_policy_t policy;
} ipc_publisher_state_t;
```


# SUBSCRIPTION STRUCTURE

A subscription requires at least:

```c
typedef struct ipc_subscription {
    ipc_endpoint_t *publisher;
    ipc_endpoint_t *subscriber;

    ipc_subscription_policy_t requested_policy;
    ipc_subscription_policy_t effective_policy;

    uint64_t timeout;
    uint64_t missed_events;

    subscription_state_t state;

    ...
} ipc_subscription_t;
```


# ACTIVE PUBLICATION STRUCTURE

An outstanding publication requires sufficient state to remember:

```text
message payload
original sender ID
which subscribers remain unresolved
publication ordering and lifetime information
```

Conceptually:

```c
typedef struct ipc_publication {
    ipc_sender_id_t sender;

    uint64_t words[4];

    atomic_size_t pending;

    publication_delivery_set_t deliveries;

    ...
} ipc_publication_t;
```

The publisher cannot simply retry every subscriber when waking because subscribers which already received the publication must not receive it again.


# LOCKING

Each endpoint retains its own queue lock.

An ordinary endpoint and its associated meta endpoint use separate locks.

The implementation should avoid holding both simultaneously where possible.

The notification-pending state crosses the two objects and therefore requires a carefully defined synchronization protocol.

Publication state introduces additional concurrency between:

```text
publishing thread
subscriber receiver
timeout processing
endpoint destruction
unsubscribe
multiple CPUs
```

The implementation must define clear lock ordering and/or atomic state transitions.


# REQUIRED CONCURRENCY INVARIANTS

The following are part of the design semantics:

```text
A notifyable endpoint has at most one outstanding meta notification.

Consuming a meta notification cannot cause a concurrent readiness event
to be permanently lost.

Stale readiness notifications are allowed.

A publication is delivered at most once to each subscriber.

Each subscriber resolves its participation in a publication exactly once.

The publication pending count is decremented exactly once per resolved
subscriber.

The publisher wakes exactly when pending reaches zero.

Timeout racing with successful delivery resolves in exactly one direction.

Unsubscribe or destruction racing with delivery resolves in exactly one
direction.

Publications on a publisher endpoint retain ordering.

A later publication does not begin fan-out until the previous publication
has resolved.

The original sender ID is preserved through every IPC transformation.
```


# OVERALL MODEL

The system consists of one underlying IPC endpoint abstraction with four modes:

```text
NORMAL
    ordinary IPC
    userspace-controlled five-word payload

META
    kernel-generated readiness queue
    original sender ID preserved

SUBSCRIBER
    kernel-controlled pub/sub receive queue
    original sender ID preserved
    word 0 reserved for kernel metadata
    words 1-4 carry publication payload

PUBLISHER
    controlled publication endpoint
    authorised users may publish four-word payloads
    publications fan out progressively
```

Meta endpoints provide IPC multiplexing.

Subscriber endpoints provide pub/sub using ordinary FIFO semantics.

Publisher endpoints provide controlled fan-out.

Per-subscriber policy determines how a full subscriber queue affects publication:

```text
RELIABLE

    The subscriber must receive every publication.

    A full queue leaves that subscriber pending.

    The publisher blocks until the subscriber accepts the publication.


TIMEOUT

    Behaves like RELIABLE initially.

    Timeout measures only the period during which this subscriber
    prevents the current publication from completing.

    If the timeout expires first, the subscriber is detached.

    Missed publications are counted and later reported with IPC_TIMEOUT.


LOSSY

    Never blocks publication.

    If full, discard the oldest publication.

    Increment missed_events.

    The next received publication reports IPC_OVERRUN and provides the
    missed count in word 0.
```

Publication is progressive.

Fast subscribers receive a publication immediately even when another subscriber blocks it.

The publishing thread remains blocked until every subscription has either received the publication or resolved according to its policy.

The sender ID remains orthogonal to all of this.

It is always transported separately from the five message words and always identifies the original sender.
