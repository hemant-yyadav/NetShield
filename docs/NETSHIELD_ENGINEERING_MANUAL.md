# NetShield Engineering Manual

Version: 1.0  
Audience: Beginners, students, and engineers onboarding to the NetShield codebase  
Codebase root: `Netshield/`  

---

## How to Use This Manual

This document is written as a teaching book for someone who starts from zero.
It focuses on:

- **What problem each part solves**
- **Why each tool/language was selected**
- **How each module works**
- **Where exactly in code to verify behavior**

Every major topic includes direct file references in backticks (for example, `api/main.py`) so you can jump from concept to implementation quickly.

---

## Table of Contents

1. Learning Goals and Mental Model  
2. The Problem NetShield Solves  
3. Big Picture Design and Runtime Topology  
4. Why This Tech Stack Was Chosen  
5. Repository Tour (Folder by Folder)  
6. System Startup Sequence  
7. Data Model and Database Strategy  
8. Proxy Engine Deep Dive (`proxy/`)  
9. Backend API Deep Dive (`api/`)  
10. Frontend Deep Dive (`frontend/`)  
11. Vault Cryptography Engine Deep Dive (`api/encryptor/`)  
12. Threat Intelligence Pipeline Deep Dive  
13. End-to-End Flows (Step-by-Step Lifecycles)  
14. Design Tradeoffs and Engineering Decisions  
15. Known Gaps, Risks, and Improvement Roadmap  
16. Code Reference Atlas (File-by-File)  
17. Practical Onboarding Checklist  
18. Glossary of Terms  

---

## 1) Learning Goals and Mental Model

Before reading code, build this model:

- NetShield is a **multi-component security platform**.
- The **C proxy** is where traffic enters and gets allowed/blocked.
- The **Python API** is a bridge for analytics, vault operations, and threat intel management.
- The **React frontend** is a control plane and observability UI.
- A shared **SQLite database** is the system spine connecting proxy decisions with dashboard visibility.

Core mindset:

1. Traffic happens in proxy.
2. Proxy writes decisions to database.
3. API reads and serves this data.
4. Frontend visualizes in near real-time.
5. Vault subsystem encrypts/decrypts files through a native C++ crypto engine.

Reference files:

- `proxy/main.c`
- `api/main.py`
- `api/websockets_manager.py`
- `frontend/src/components/Dashboard.jsx`
- `api/routers/vault.py`
- `api/encryptor/main.cpp`

---

## 2) The Problem NetShield Solves

### 2.1 Primary Problems

NetShield targets three connected problems:

1. **Traffic enforcement problem**  
   Organizations or individuals need to block malicious/tracking domains at gateway level.

2. **Visibility problem**  
   Blocking without observability is hard to trust. Operators need live analytics (counts, logs, trends).

3. **Secure handling problem**  
   Potentially risky files/payloads should be encrypted, isolated, and shared safely.

### 2.2 Why Build This System Instead of Separate Tools?

You could run a standalone proxy + standalone dashboard + standalone file vault, but that causes:

- split context
- harder debugging
- weak interoperability
- duplicated storage

NetShield integrates these concerns in one architecture:

- proxy action produces analytics immediately
- threat updates affect blocking behavior directly
- intercepted payloads can be routed into vault flows

### 2.3 Target Users

- Security lab students
- SOC prototypes / blue team experimentation
- Developers learning full-stack + systems + crypto integration
- Recruiter-demo context where one project shows networking, backend, frontend, and cryptography

---

## 3) Big Picture Design and Runtime Topology

### 3.1 Runtime Components

Defined by `docker-compose.yml`:

- `proxy` service built from `proxy/`
- `api` service built from `api/`
- `frontend` service built from `frontend/`

Shared volumes:

- `netshield-data` (contains SQLite DB at `/data/netshield.db`)
- `vault-data` (encrypted and temporary vault files at `/vault`)

### 3.2 Layered Architecture

Layer 1 (Traffic Plane):
- `proxy/main.c`

Layer 2 (Data + Control Plane):
- SQLite DB (`/data/netshield.db`)
- FastAPI endpoints (`api/routers/*.py`)
- Background websocket poller (`api/websockets_manager.py`)

Layer 3 (Presentation Plane):
- React UI (`frontend/src/components/*`)

Layer 4 (Native Crypto Subsystem):
- C++ encryptor binary (`api/encryptor/main.cpp`)

### 3.3 Why This Layering Works

- The traffic path remains low-level and performant in C.
- Business and integration logic stays quick to iterate in Python.
- UX and data visualization stay independent in React.
- Heavy cryptographic operations stay in compiled C++/OpenSSL.

This gives practical division of concerns:

- performance-sensitive code in C/C++
- API orchestration in Python
- interaction and telemetry rendering in frontend JS

---

## 4) Why This Tech Stack Was Chosen

### 4.1 C for Proxy (`proxy/main.c`)

**Why**:

- Direct control over sockets and byte streams.
- Predictable low-level behavior for HTTP/CONNECT handling.
- Easy use of `pthread`, `select`, and system calls.

**Problem solved**:

- Build a network gateway that can inspect and route requests with minimal abstraction overhead.

### 4.2 Python + FastAPI for API (`api/`)

**Why**:

- Fast development loop.
- Native support for REST and WebSocket endpoints.
- Clean dependency injection for DB sessions.

**Problem solved**:

- Expose analytics, threat controls, and vault operations quickly without reimplementing HTTP stack in C.

### 4.3 SQLite as Shared Persistence (`schema.sql`, `api/database.py`)

**Why**:

- Zero external DB server.
- Easy to mount as a single file in containers.
- Supported in both C (`sqlite3`) and Python (SQLAlchemy + sqlite3 module).

**Problem solved**:

- Proxy and API can read/write same state without complex infra.

### 4.4 React + Vite for Frontend (`frontend/`)

**Why**:

- Component model for multi-page dashboard tabs.
- Vite gives fast dev reload and simple API proxy configuration.
- Ecosystem tools (Recharts, Framer Motion, Lucide icons).

**Problem solved**:

- Build an operator-facing UI that updates quickly and remains maintainable.

### 4.5 OpenSSL C++ for Crypto (`api/encryptor/main.cpp`)

**Why**:

- Strong audited primitives (AES-GCM, PBKDF2, HMAC).
- Native binary allows deterministic behavior from API subprocess calls.

**Problem solved**:

- Secure file encryption/decryption with authenticated encryption and token signing.

---

## 5) Repository Tour (Folder by Folder)

### Root

- `docker-compose.yml`  
  Service orchestration and volume mapping.
- `.env`  
  Environment defaults for ports and paths.
- `schema.sql`  
  Base DB schema loaded by API init when DB file does not exist.
- `Makefile`  
  Convenience commands for container lifecycle.

### `proxy/` (primary runtime proxy)

- `main.c`  
  Main server logic, threat checks, DB logs, interception.
- `proxy_parse.c`, `proxy_parse.h`  
  HTTP request parsing utilities.
- `Makefile`, `Dockerfile`  
  Build and runtime setup.

### `api/`

- `main.py`  
  App creation and startup lifecycle.
- `database.py`, `models.py`, `schemas.py`  
  Data access and model contracts.
- `routers/traffic.py`  
  Logs, stats, chart, websocket endpoint.
- `routers/threats.py`  
  Threat sync, check, add custom domain, status/stat endpoints.
- `routers/vault.py`  
  File upload/share/decrypt workflows.
- `websockets_manager.py`  
  Background DB polling + broadcasting.
- `threat_updater.py`  
  External threat list ingestion.
- `encryptor/main.cpp`  
  Native encryption/token engine.

### `frontend/`

- `src/App.jsx`  
  Tab switch root component.
- `src/components/Dashboard.jsx`  
  Live traffic panel.
- `src/components/Threats.jsx`  
  Threat intelligence management.
- `src/components/Vault.jsx`  
  Vault UX (upload/share/decrypt).
- `src/components/Sidebar.jsx`  
  Navigation.
- `src/index.css`  
  Global styling and panel style system.

### Additional Included Projects

- `Proxy server/`  
  Alternate older proxy implementation with LRU caching behavior.
- `encrypty/`  
  Separate experimental encryption project using shared memory + fork + semaphores.

These are useful for learning but are not the primary production path in `docker-compose.yml`.

---

## 6) System Startup Sequence

### 6.1 Container Boot

File: `docker-compose.yml`

1. Build `proxy` image from `proxy/`.
2. Build `api` image from `api/`.
3. Build `frontend` image from `frontend/`.
4. Mount shared volumes.
5. Expose ports:
   - proxy: `${PROXY_PORT:-8888}`
   - api: `${API_PORT:-8000}`
   - frontend: `3000`

### 6.2 API Boot and DB Init

File: `api/main.py`

Boot path:

1. `init_db()` is called at import time.
2. If DB file missing, it tries to load `schema.sql` (`/app/schema.sql` or `../schema.sql`).
3. Calls `models.Base.metadata.create_all(bind=engine)` for ORM-defined tables.
4. Creates FastAPI app.
5. Includes routers:
   - `traffic.router`
   - `vault.router`
   - `threats.router`
6. On startup event, spawns `poll_database_for_updates()` task.

### 6.3 Proxy Boot

File: `proxy/main.c`

Boot path:

1. Reads `PROXY_PORT` env (default 8888).
2. Reads `DB_PATH` env (default `/data/netshield.db`).
3. Spawns worker threads (`NUM_THREADS`).
4. Binds listening socket and enters accept loop.
5. Accepted sockets are enqueued for worker processing.

### 6.4 Frontend Boot

Files: `frontend/src/main.jsx`, `frontend/src/App.jsx`

1. React root mounts app.
2. Sidebar controls active tab state.
3. Components trigger their own fetch/poll/websocket behavior.

---

## 7) Data Model and Database Strategy

### 7.1 Core Tables

Schema file: `schema.sql`  
ORM file: `api/models.py`

Conceptual tables:

- `traffic_logs`: every observed request decision.
- `threats`: blocked domain intelligence.
- `vault_files`: encrypted file metadata.
- `system_stats`: key-value counters.
- `threat_intel_stats`: sync metrics (declared in ORM, used by threats router/updater).

### 7.2 Why a Shared DB Matters

The DB is the common truth between independent executables:

- C proxy writes events.
- Python API reads/aggregates events.
- UI requests API summaries.

Without this, you would need a message broker or custom IPC.

### 7.3 Important Practical Observation

There are schema naming differences between `schema.sql` and ORM/sql usage in Python/C modules.
For maintainers, this is a key area to normalize first.

Affected files:

- `schema.sql`
- `api/models.py`
- `api/threat_updater.py`
- `api/routers/threats.py`
- `proxy/main.c`

---

## 8) Proxy Engine Deep Dive (`proxy/`)

Primary file: `proxy/main.c`

### 8.1 Core Responsibilities

- Accept client connections.
- Parse request type (CONNECT vs non-CONNECT).
- Check domain against active threats.
- Allow or deny request.
- Log traffic decision to database.
- Optionally intercept downloadable response payloads.

### 8.2 Concurrency Model

Mechanisms used:

- fixed-size queue (`client_queue`, `queue_head`, `queue_tail`)
- synchronization:
  - `pthread_mutex_t queue_mutex`
  - `pthread_cond_t queue_cond`
- worker thread function: `worker_thread()`

Why this model:

- avoids spawning a thread per connection
- bounds memory and scheduling overhead
- simple producer/consumer design

### 8.3 Threat Decision Logic

Function: `is_domain_blocked(sqlite3 *db, const char *domain)`

Workflow:

1. Normalize target by stripping `www.` prefix when present.
2. Execute SQL query against `threats`.
3. Return blocked flag.

Why this function exists:

- central decision point used by both CONNECT and HTTP request flows.

### 8.4 Request Path Handling

#### CONNECT Path (usually HTTPS tunnels)

Code section: inside `worker_thread()`, when request starts with `"CONNECT"`

Behavior:

- extract host and port
- evaluate block status
- if blocked:
  - return `403 Forbidden`
  - log blocked event
- if allowed:
  - connect remote
  - call `handle_https()` to relay bytes both directions
  - log allowed event

#### Non-CONNECT Path (HTTP)

Code uses parser from `proxy_parse.c`.

Behavior:

- parse request
- ensure host header and connection behavior
- block decision against `req->host`
- forward to remote
- inspect response headers for interception candidates

### 8.5 Download/Payload Interception

Key logic in `proxy/main.c`:

- Detects candidate responses via:
  - `Content-Disposition: attachment`
  - selected binary MIME types
- Forks child process and pipes response body to child stdin.
- Child executes curl command posting stream to:
  - `http://api:8000/api/vault/share/generate`

Why this feature exists:

- convert suspicious or downloadable content into secured vault-managed artifacts.

### 8.6 Database Logging in Proxy

Function: `log_to_db(...)`

Writes:

- domain
- method
- status code
- request and response sizes
- blocked flag
- client IP
- timestamp

This powers:

- `GET /api/traffic/logs`
- `GET /api/traffic/stats`
- websocket live updates

---

## 9) Backend API Deep Dive (`api/`)

### 9.1 Application Core

File: `api/main.py`

Main jobs:

- initialize DB
- configure CORS
- register routers
- start websocket polling task

### 9.2 Database Session and ORM

File: `api/database.py`

- Loads `DB_PATH`.
- Creates SQLAlchemy engine (`sqlite:///...`).
- Provides `SessionLocal`.
- Defines `get_db()` dependency to ensure session closure.

File: `api/models.py`

Defines ORM entities:

- `TrafficLog`
- `SystemStat`
- `Threat`
- `ThreatIntelligenceStat`
- `VaultFile`

### 9.3 Traffic Router

File: `api/routers/traffic.py`

Endpoints:

- `GET /api/traffic/logs`  
  Returns recent traffic records.
- `GET /api/traffic/stats`  
  Returns total/blocked/allowed counts and system stats map.
- `GET /api/traffic/chart`  
  Aggregates last 24 hours into hourly buckets.
- `WS /api/traffic/live`  
  WebSocket endpoint used by dashboard.

Why this router exists:

- single API surface for observability data.

### 9.4 Threats Router

File: `api/routers/threats.py`

Endpoints:

- `POST /api/threats/refresh`  
  Starts background sync from external feeds.
- `GET /api/threats/status`  
  Returns sync progress and latest update counters.
- `GET /api/threats/stats`  
  Returns total blocked domains, source counts.
- `GET /api/threats/recent`  
  Returns latest added threat rows.
- `GET /api/threats/check?domain=...`  
  Validates domain syntax and checks block status.
- `POST /api/threats/add`  
  Adds custom blocked domain manually.

Why this router exists:

- threat lifecycle management without direct DB access.

### 9.5 Vault Router

File: `api/routers/vault.py`

Endpoints:

- `POST /api/vault/upload`  
  Personal vault mode; encrypts with user password.
- `POST /api/vault/share/generate`  
  Shared mode; encrypts with server master password and creates signed token link.
- `GET /api/vault/share/download/{token}`  
  Verifies token, enforces one-time retrieval, decrypts, returns file.
- `GET /api/vault/files`  
  Lists vault records.
- `POST /api/vault/decrypt`  
  Decrypts personal vault file with user password.

Why subprocess bridge is used:

- Python route calls native binary (`ENCRYPTOR_BIN`) to keep strong crypto in C++.

---

## 10) Frontend Deep Dive (`frontend/`)

### 10.1 App Shell

Files:

- `frontend/src/main.jsx`
- `frontend/src/App.jsx`
- `frontend/src/components/Sidebar.jsx`

Tab architecture:

- Dashboard
- Threats
- Vault

### 10.2 Dashboard Component

File: `frontend/src/components/Dashboard.jsx`

Behavior:

1. Initial baseline fetch:
   - `/api/traffic/stats`
   - `/api/traffic/logs?limit=50`
   - `/api/traffic/chart`
2. Opens websocket to `/api/traffic/live`.
3. On `new_log`, prepends row to log list.
4. On `update_stats`, updates counters and refreshes chart.

Why this dual approach (HTTP baseline + WS incremental):

- baseline guarantees first render has context
- websocket gives continuous updates without heavy polling

### 10.3 Threats Component

File: `frontend/src/components/Threats.jsx`

Behavior:

- Fetches stats and recent threats.
- Polls sync status every 2 seconds.
- Triggers refresh endpoint.
- Supports domain check and custom domain add.

Why this UX:

- operator can manage intelligence lifecycle and instantly validate domain status.

### 10.4 Vault Component

File: `frontend/src/components/Vault.jsx`

Modes:

- Personal Vault mode:
  - upload with password (`/api/vault/upload`)
  - decrypt with password (`/api/vault/decrypt`)
- Public Share mode:
  - upload for JWT link generation (`/api/vault/share/generate`)
  - one-time download by token endpoint

UX details:

- drag and drop upload
- animated cards
- modal password prompts
- file list refresh

---

## 11) Vault Cryptography Engine Deep Dive (`api/encryptor/`)

Primary file: `api/encryptor/main.cpp`

### 11.1 Why Native C++ for Crypto

It uses OpenSSL EVP APIs directly, offering:

- authenticated encryption (AES-GCM)
- robust KDF (PBKDF2-HMAC-SHA256)
- efficient stream processing for files

### 11.2 Encryption Path

Functions:

- `derive_key(...)`
- `do_encrypt(...)`

Process:

1. generate random salt and IV
2. derive 256-bit key from password + salt
3. encrypt stream using AES-256-GCM
4. append GCM tag
5. output metadata in fixed binary sequence

### 11.3 Decryption Path

Function:

- `do_decrypt(...)`

Process:

1. read salt + IV from file header
2. derive key from user password
3. decrypt ciphertext
4. validate authentication tag
5. if tag check fails, delete output file

### 11.4 Token Signing and Verification

Modes:

- `sign_token`
- `verify_token`

Approach:

- custom JWT-like string format:
  - header.payload.signature
- base64url encoding
- HMAC-SHA256 signature
- payload carries:
  - `file_id`
  - `exp`
  - `one_time`

Why this is used:

- API needs stateless share-link validation tied to cryptographic integrity.

---

## 12) Threat Intelligence Pipeline Deep Dive

### 12.1 Updater Script

File: `api/threat_updater.py`

Sources:

- StevenBlack hosts file
- URLHaus hostfile

Process:

1. fetch list text from remote URLs
2. parse line-by-line and extract domains
3. map source/category metadata
4. bulk insert with `INSERT OR IGNORE`
5. write sync metrics into `threat_intel_stats`

### 12.2 API-Triggered Background Sync

File: `api/routers/threats.py`

- `POST /api/threats/refresh` schedules `bg_sync_threats()` using FastAPI background tasks.
- status is held in in-memory `sync_state` + DB metrics.

Why background mode:

- avoid long blocking request while remote list download happens.

### 12.3 Proxy Consumption of Threat Data

File: `proxy/main.c`

- each incoming domain is checked live against `threats`.
- no app restart needed if DB updates are in place.

---

## 13) End-to-End Flows (Step-by-Step Lifecycles)

### 13.1 Flow A: Normal Allowed Traffic to Dashboard

1. Client sends request to proxy port.
2. `proxy/main.c` worker reads request.
3. Proxy checks `threats`; domain not blocked.
4. Proxy forwards request and relays response.
5. Proxy logs allowed event in `traffic_logs`.
6. API websocket poller sees new row.
7. API broadcasts `new_log` and `update_stats`.
8. `Dashboard.jsx` updates table and counters.

Code anchors:

- `proxy/main.c`
- `api/websockets_manager.py`
- `api/routers/traffic.py`
- `frontend/src/components/Dashboard.jsx`

### 13.2 Flow B: Blocked Domain

1. Request enters proxy.
2. Threat query returns positive match.
3. Proxy responds `403 Forbidden`.
4. Proxy writes blocked log row.
5. Dashboard receives blocked update and increments blocked counter.

Code anchors:

- `proxy/main.c:is_domain_blocked`
- `proxy/main.c:log_to_db`
- `frontend/src/components/Dashboard.jsx`

### 13.3 Flow C: Threat Feed Refresh

1. Operator clicks refresh in Threats UI.
2. UI posts to `/api/threats/refresh`.
3. API launches background sync task.
4. `threat_updater.py` fetches remote feeds and inserts domains.
5. UI polls `/api/threats/status` and `/api/threats/stats`.
6. New domains become active for proxy checks.

Code anchors:

- `frontend/src/components/Threats.jsx`
- `api/routers/threats.py`
- `api/threat_updater.py`
- `proxy/main.c`

### 13.4 Flow D: Personal Vault Upload and Decrypt

1. User selects file in vault personal mode.
2. Frontend sends multipart form (`file`, `password`) to `/api/vault/upload`.
3. API writes temporary file.
4. API runs C++ binary in `encrypt` mode.
5. API stores DB metadata row in `vault_files`.
6. Later user requests decrypt with password via `/api/vault/decrypt`.
7. API runs C++ binary in `decrypt` mode and returns file response.

Code anchors:

- `frontend/src/components/Vault.jsx`
- `api/routers/vault.py`
- `api/encryptor/main.cpp`

### 13.5 Flow E: Share Link Generation and One-Time Download

1. User uploads in share mode with expiry.
2. API encrypts file using server master password.
3. API calls binary `sign_token` mode.
4. API stores token and expiry in DB.
5. API returns share URL to UI.
6. Download endpoint verifies token and checks retrieval status.
7. Endpoint marks file as retrieved and decrypts with master password.
8. File is returned to client.

Code anchors:

- `api/routers/vault.py`
- `api/encryptor/main.cpp`
- `frontend/src/components/Vault.jsx`

---

## 14) Design Tradeoffs and Engineering Decisions

### 14.1 Why SQLite Instead of PostgreSQL/Redis Combo

Chosen for simplicity and portability:

- one file persistence
- no extra service to manage
- easy local setup

Tradeoff:

- high-concurrency behavior is weaker than server databases
- schema drift is easier if not strongly governed

### 14.2 Why Polling for WebSocket Updates Instead of Event Bus

Chosen for straightforward implementation:

- API can poll same DB already used by proxy

Tradeoff:

- periodic query overhead
- eventual consistency by polling interval (not strict instant event stream)

### 14.3 Why Subprocess for Crypto

Chosen to isolate crypto implementation in one native executable.

Tradeoff:

- process spawn overhead
- argument handling and output parsing complexity

### 14.4 Why C Proxy + Python API Split

Chosen to align strengths:

- C for network performance/control
- Python for rapid service development

Tradeoff:

- cross-language contracts must stay synchronized (DB schema and semantics)

---

## 15) Known Gaps, Risks, and Improvement Roadmap

This section is intentionally practical for maintainers.

### 15.1 Schema Consistency Program (Priority 0)

Normalize schema contracts between:

- `schema.sql`
- `api/models.py`
- SQL statements in:
  - `api/threat_updater.py`
  - `api/routers/threats.py`
  - `proxy/main.c`

Action:

- define one canonical migration source
- update all SQL and ORM names to match
- add startup validation tests

### 15.2 Security Hardening Program

Targets:

- replace SQL string interpolation in `proxy/main.c:log_to_db` with prepared statements
- remove shell-command string risk in proxy interception execution path
- add auth for API endpoints (`vault`, `threats`)
- tighten CORS config in `api/main.py`

### 15.3 Observability and Reliability Program

Targets:

- add structured logs with correlation IDs
- add health and readiness endpoints
- add retry/backoff and telemetry around threat feed fetches
- add cleanup policy for temporary decrypted files

### 15.4 Scalability Program

Targets:

- move from SQLite to PostgreSQL when multi-writer pressure grows
- replace DB poll loop with event-driven transport
- support additional HTTP methods and robust request parsing

---

## 16) Code Reference Atlas (File-by-File)

Use this as an index to jump directly into code.

### Root

- `docker-compose.yml`  
  Container graph, volumes, and ports.
- `.env`  
  Runtime variable defaults.
- `schema.sql`  
  Initial table/index definitions.
- `Makefile`  
  Common Docker commands.

### Proxy (`proxy/`)

- `proxy/main.c`
  - queue management: `enqueue`, `dequeue`
  - remote connect: `connectRemoteServer`
  - blocking decision: `is_domain_blocked`
  - DB event persistence: `log_to_db`
  - HTTPS tunnel relay: `handle_https`
  - worker pipeline: `worker_thread`
  - process lifecycle: `main`
- `proxy/proxy_parse.c`
  - request parser internals and request reconstruction
- `proxy/proxy_parse.h`
  - parser structs and exported parser functions
- `proxy/Makefile`
  - compiles proxy and parser with sqlite3 linking
- `proxy/Dockerfile`
  - alpine build + runtime executable

### API (`api/`)

- `api/main.py`
  - DB initialization and FastAPI app setup
- `api/database.py`
  - SQLAlchemy engine/session configuration
- `api/models.py`
  - ORM entities
- `api/schemas.py`
  - pydantic response models
- `api/websockets_manager.py`
  - live push manager and DB polling loop
- `api/threat_updater.py`
  - external feed ingestion and DB writes
- `api/routers/traffic.py`
  - traffic endpoints and WS route
- `api/routers/threats.py`
  - threat management endpoints
- `api/routers/vault.py`
  - file encryption/decryption/share endpoints
- `api/encryptor/main.cpp`
  - AES-GCM and token logic
- `api/encryptor/Makefile`
  - native binary build
- `api/Dockerfile`
  - API image build and uvicorn command

### Frontend (`frontend/`)

- `frontend/vite.config.js`
  - local dev server and API proxy
- `frontend/src/main.jsx`
  - React entrypoint
- `frontend/src/App.jsx`
  - tab routing composition
- `frontend/src/components/Sidebar.jsx`
  - navigation
- `frontend/src/components/Dashboard.jsx`
  - live analytics view
- `frontend/src/components/Threats.jsx`
  - threat workflow view
- `frontend/src/components/Vault.jsx`
  - vault workflow view
- `frontend/src/index.css`
  - visual system and page styles

### Additional Included Modules (Learning/Legacy)

- `Proxy server/proxyserver.c`
  - alternate multithreaded proxy with LRU cache
- `encrypty/main.cpp`
  - standalone batch crypt processing entrypoint
- `encrypty/src/app/processes/ProcessManagement.cpp`
  - shared memory + fork queue manager
- `encrypty/src/app/encryptDecrypt/Cryption.cpp`
  - simple byte-shift crypt routine

---

## 17) Practical Onboarding Checklist

Use this sequence when onboarding a new developer:

1. Read this manual sections 2 through 5.
2. Open `docker-compose.yml` and map services.
3. Trace startup from `api/main.py` and `proxy/main.c`.
4. Run UI and inspect:
   - dashboard flow
   - threat refresh flow
   - vault upload/decrypt flow
5. Validate schema alignment between SQL and ORM.
6. Add one small endpoint in `api/routers/traffic.py`.
7. Expose it in frontend component.
8. Add one automated sanity test script for route behavior.

---

## 18) Glossary of Terms

- **Gateway Proxy**: service that sits between client and destination server, enforcing policies.
- **CONNECT**: HTTP method used to create a TCP tunnel (commonly HTTPS through proxy).
- **Threat Intelligence Feed**: external domain list used to classify/block malicious/tracker domains.
- **AES-GCM**: authenticated encryption mode (confidentiality + integrity).
- **PBKDF2**: key derivation algorithm turning passwords into cryptographic keys.
- **HMAC**: keyed hash used for message integrity and token signatures.
- **WebSocket**: persistent bi-directional protocol for push updates.
- **ORM**: object relational mapper (`api/models.py` via SQLAlchemy).
- **One-time Link**: share token intended to allow a single retrieval.

---

## Final Notes for Readers

If you are new:

- Start from `docker-compose.yml` to understand system boundaries.
- Then read `proxy/main.c` and `api/main.py` side-by-side.
- Use this manual's references to navigate each flow.

If you are maintaining:

- Prioritize schema normalization and security hardening first.
- Add tests around threat sync and vault token verification.

If you are evaluating:

- NetShield demonstrates integration across systems programming, backend APIs, frontend observability, and native cryptography in one cohesive project.

---

## 19) Chapter-by-Chapter Guided Reading Plan

This chapter gives a practical "study route" for beginners.

### Week 1 Plan (Concept + Runtime)

Day 1:

- Read `docker-compose.yml`.
- Draw three boxes: Proxy, API, Frontend.
- Mark shared resources:
  - `/data/netshield.db`
  - `/vault`

Day 2:

- Read `api/main.py` slowly.
- Identify:
  - DB bootstrap
  - router inclusion
  - startup background task

Day 3:

- Read `proxy/main.c` top to bottom.
- Mark in notebook:
  - where requests are accepted
  - where block decision is made
  - where DB write happens

Day 4:

- Read `frontend/src/components/Dashboard.jsx`.
- Observe mapping from API/WebSocket payloads to table/chart UI.

Day 5:

- Read `api/routers/threats.py` + `api/threat_updater.py`.
- Verify full refresh workflow.

Day 6:

- Read `api/routers/vault.py`.
- Follow upload/decrypt/share route logic.

Day 7:

- Read `api/encryptor/main.cpp`.
- Write your own summary of encrypt/decrypt/sign/verify modes.

### Week 2 Plan (Deep Internalization)

- Compare schema/ORM/SQL for all threat and vault columns.
- Run one "flow trace" from UI action to DB write.
- Document one bug candidate and one improvement candidate.

---

## 20) What Each Tool Solves (with Alternatives)

This section focuses heavily on **why this specific tool** was picked.

### 20.1 Docker Compose

Used file: `docker-compose.yml`

Why:

- NetShield has multiple services requiring network and volume wiring.
- Compose allows reproducible local setup.

Alternative:

- Manual `docker run` commands or shell scripts.

Why not alternative:

- Harder to maintain and explain to new contributors.

### 20.2 FastAPI

Used in:

- `api/main.py`
- `api/routers/*.py`

Why:

- clean route declaration
- async support for websocket and startup tasks
- minimal boilerplate

Alternative:

- Flask + extensions, Django REST, Node/Express.

Why this fit:

- lightweight and clear for service-layer orchestration.

### 20.3 SQLAlchemy

Used in:

- `api/database.py`
- `api/models.py`
- partial use in routers (`traffic.py`, `vault.py`)

Why:

- model-driven table definitions
- easier migrations/refactors over raw SQL-only approach

Alternative:

- raw sqlite3 everywhere

Why mixed approach happened:

- project combines older direct SQL usage with newer ORM usage.
- this is powerful but must be normalized.

### 20.4 sqlite3 C API in Proxy

Used in:

- `proxy/main.c`

Why:

- direct and fast DB interaction in C.
- no extra service dependency.

Alternative:

- send logs over HTTP to API and only API writes DB.

Why current design:

- lower complexity in immediate proxy event persistence.

### 20.5 React + Component Architecture

Used in:

- `frontend/src/App.jsx`
- `frontend/src/components/*`

Why:

- clear separation of feature panels (Dashboard, Threats, Vault).
- stateful interactive UI.

Alternative:

- server-rendered templates or vanilla JS SPA.

Why current choice:

- easier to evolve and scale UI interactions.

### 20.6 OpenSSL (C++)

Used in:

- `api/encryptor/main.cpp`

Why:

- battle-tested primitives and APIs.
- explicit control over cryptographic operations.

Alternative:

- Python cryptography packages only.

Why hybrid approach:

- native crypto in one binary reused by multiple API operations.

---

## 21) Detailed Design Flow (From Requirement to Module)

This chapter shows likely requirement-to-code mapping.

### Requirement A: "Block known malicious domains"

Design choices:

- store blocked domains in DB table
- check domain for each request in proxy
- respond with 403 if blocked

Implemented in:

- `api/threat_updater.py` (populate domain list)
- `api/routers/threats.py` (manual add/check/sync trigger)
- `proxy/main.c:is_domain_blocked` (enforcement)

### Requirement B: "Show live analytics dashboard"

Design choices:

- persist each decision in `traffic_logs`
- expose REST baseline endpoints
- push incremental updates using websocket

Implemented in:

- `proxy/main.c:log_to_db`
- `api/routers/traffic.py`
- `api/websockets_manager.py`
- `frontend/src/components/Dashboard.jsx`

### Requirement C: "Encrypt and securely share files"

Design choices:

- native AES-GCM encryption engine
- API routes orchestrate file handling and metadata
- tokenized share links with expiry and one-time semantics

Implemented in:

- `api/encryptor/main.cpp`
- `api/routers/vault.py`
- `frontend/src/components/Vault.jsx`

### Requirement D: "Operator-manageable threat intelligence"

Design choices:

- external source integration
- asynchronous refresh
- status and stats endpoints

Implemented in:

- `api/threat_updater.py`
- `api/routers/threats.py`
- `frontend/src/components/Threats.jsx`

---

## 22) Function-Level Walkthrough: Proxy Core

File: `proxy/main.c`

### `enqueue(int client_socket)` and `dequeue()`

Purpose:

- Implements producer/consumer queue for accepted sockets.

Why:

- decouples accept loop from connection processing.

### `connectRemoteServer(char *host_addr, int port_num)`

Purpose:

- resolves remote host and opens connection.

Why:

- central helper reused by CONNECT and normal HTTP paths.

### `is_domain_blocked(sqlite3 *db, const char *domain)`

Purpose:

- query threat table and return binary decision.

Why:

- local synchronous enforcement where traffic enters.

### `log_to_db(...)`

Purpose:

- audit each request decision for analytics.

Why:

- dashboard and metrics depend on consistent event stream.

### `handle_https(int client_sock, int remote_sock)`

Purpose:

- bidirectional byte relay for CONNECT tunnel.

Why:

- maintain end-to-end TLS traffic pass-through while still controlling domain-level access.

### `worker_thread(void *arg)`

Purpose:

- execute full per-connection workflow.

Contains:

- client request receive
- CONNECT branch
- parser branch
- block decisions
- forwarding
- interception logic
- DB logging

### `main()`

Purpose:

- initialize runtime and start accept loop.

---

## 23) Function-Level Walkthrough: API Core

### File: `api/main.py`

#### `init_db()`

Purpose:

- initialize DB file and schema when missing.

Why:

- bootstraps first run without external manual SQL step.

#### startup event

- launches websocket polling background task.

Why:

- keeps live feed system always active while API is up.

### File: `api/websockets_manager.py`

#### `ConnectionManager`

Purpose:

- track active websocket clients and broadcast payloads.

Methods:

- `connect`
- `disconnect`
- `broadcast_log`
- `broadcast_stats`

#### `poll_database_for_updates()`

Purpose:

- detect new traffic rows and push updates.

Why:

- glue between DB writes in proxy and UI real-time behavior.

### File: `api/routers/traffic.py`

#### `get_traffic_logs`
- paginated log retrieval

#### `get_traffic_stats`
- aggregated counters

#### `get_traffic_chart`
- hourly bucketization for 24h graph

#### websocket endpoint
- lifecycle for `/api/traffic/live`

### File: `api/routers/threats.py`

#### `bg_sync_threats`
- invokes updater, updates in-memory status.

#### `trigger_sync`
- async refresh trigger.

#### `get_sync_status`, `get_threat_stats`, `get_recent_threats`
- operator visibility.

#### `check_domain`
- regex validation + DB lookup.

#### `add_custom_threat`
- insert custom domain with guard against duplicates.

### File: `api/routers/vault.py`

#### `upload_file`
- personal encryption with user password.

#### `share_generate`
- server-password encryption + signed token generation.

#### `share_download`
- token verify + one-time check + decrypt.

#### `list_files`
- vault metadata listing.

#### `decrypt_manual_file`
- personal decrypt path with password.

---

## 24) Function-Level Walkthrough: Frontend Core

### File: `frontend/src/App.jsx`

Role:

- central tab state and conditional rendering.

Why:

- simple app-level page switching without router complexity.

### File: `frontend/src/components/Sidebar.jsx`

Role:

- interaction menu to set active tab.

Why:

- clear separation between navigation and feature panels.

### File: `frontend/src/components/Dashboard.jsx`

Role:

- observability dashboard.

Important behavior:

- baseline fetch + websocket incremental stream.
- reconnect strategy when websocket closes.

### File: `frontend/src/components/Threats.jsx`

Role:

- threat source stats and control panel.

Important behavior:

- periodic polling for refresh state.
- user-triggered domain check and custom inserts.

### File: `frontend/src/components/Vault.jsx`

Role:

- file handling UX for encryption workflows.

Important behavior:

- mode toggle (personal/share)
- drag-drop upload and form upload
- one-time/shared download and personal decrypt flow

---

## 25) Architecture Quality Notes for Recruiters and Engineers

What is technically notable:

1. Cross-language integration:
   - C (proxy) + Python (API) + JS (frontend) + C++ (crypto).
2. Real-time data plane to UI:
   - persistent telemetry loop from DB to websocket.
3. Practical security intent:
   - domain blocklist, encrypted vault, one-time retrieval semantics.
4. Systems programming + product interface:
   - low-level network handling with user-facing control dashboard.

What should be improved before production:

- schema governance
- authentication and authorization
- safer command execution and SQL construction
- stronger test coverage

---

## 26) Troubleshooting by Symptom (Beginner Friendly)

### Symptom: Dashboard shows no live updates

Check:

1. Is proxy writing `traffic_logs`?
   - inspect code path in `proxy/main.c:log_to_db`.
2. Is websocket polling task running?
   - startup event in `api/main.py`.
3. Is frontend websocket connected?
   - `Dashboard.jsx` sets `wsStatus`.

### Symptom: Threat refresh says success but blocks do not apply

Check:

1. Column name mismatches between schema and updater SQL.
2. SQL insert target fields in `api/threat_updater.py`.
3. query field names in `proxy/main.c:is_domain_blocked`.

### Symptom: Vault share download fails

Check:

1. token verification path in `api/routers/vault.py`
2. binary output behavior in `api/encryptor/main.cpp`
3. env vars for secrets/master password

### Symptom: API boots but DB data appears inconsistent

Check:

1. whether DB already existed before startup
2. interaction between `schema.sql` and `models.Base.metadata.create_all`
3. table column contracts in all SQL statements

---

## 27) Security Review Starter Guide

For each category, inspect these files.

### Input Validation

- `api/routers/threats.py` (domain regex checks)
- `api/routers/vault.py` (form fields, token path)
- `proxy/main.c` (request and header extraction)

### Authentication/Authorization

- currently minimal for API routes
- start hardening in:
  - `api/main.py`
  - all router modules

### Secret Handling

- `.env`
- `api/routers/vault.py` (`SERVER_SECRET`, `MASTER_PASSWORD`)
- subprocess argument handling in vault route calls

### Data Integrity

- `api/encryptor/main.cpp` tag verification and HMAC checks
- one-time retrieval state updates in `api/routers/vault.py`

### Command Injection / Shell Risks

- proxy interception shell command assembly in `proxy/main.c`

---

## 28) Suggested Test Matrix (What to Test and Why)

### Proxy Decision Tests

- blocked domain returns 403
- allowed domain forwards successfully
- CONNECT handling with blocked and allowed hosts

Files under test:

- `proxy/main.c`

### API Traffic Tests

- `/api/traffic/logs` pagination and ordering
- `/api/traffic/stats` counter integrity
- `/api/traffic/chart` hourly bucket correctness

Files under test:

- `api/routers/traffic.py`

### Threat Pipeline Tests

- refresh trigger and status transitions
- source count updates
- custom domain add and duplicate rejection

Files under test:

- `api/routers/threats.py`
- `api/threat_updater.py`

### Vault Tests

- personal upload/decrypt success and wrong password failure
- shared upload/token verify/expiry path
- one-time retrieval enforcement

Files under test:

- `api/routers/vault.py`
- `api/encryptor/main.cpp`

### Frontend Integration Tests

- dashboard baseline render
- websocket incremental updates
- vault mode toggle and upload actions

Files under test:

- `frontend/src/components/Dashboard.jsx`
- `frontend/src/components/Vault.jsx`
- `frontend/src/components/Threats.jsx`

---

## 29) Implementation Notes for Future Contributors

### 29.1 If You Add a New DB Field

Update all of:

1. `schema.sql`
2. `api/models.py`
3. any raw SQL in:
   - `api/routers/*.py`
   - `api/threat_updater.py`
   - `proxy/main.c`
4. frontend models/usage if displayed.

### 29.2 If You Add a New API Endpoint

Recommended pattern:

1. router function in `api/routers/...`
2. db session dependency using `get_db`
3. response schema in `api/schemas.py` if needed
4. frontend usage in specific component
5. update this manual's reference atlas

### 29.3 If You Change Crypto Behavior

Must coordinate:

1. C++ binary behavior (`api/encryptor/main.cpp`)
2. subprocess invocation and output parse (`api/routers/vault.py`)
3. metadata assumptions in DB model (`api/models.py`)
4. UX assumptions in `frontend/src/components/Vault.jsx`

---

## 30) Executive Summary (for New Team Members)

NetShield is a layered, cross-language security system:

- A **C proxy** receives traffic, enforces domain blocking, and logs outcomes.
- A **Python FastAPI service** exposes control and telemetry APIs, runs threat sync, and bridges to native crypto tooling.
- A **React frontend** provides an operator interface for real-time traffic visualization, threat operations, and secure vault workflows.
- A **C++ OpenSSL engine** handles encryption/decryption and token signatures.
- Shared **SQLite state** is the integration backbone.

If you only remember five files, remember these:

1. `proxy/main.c`
2. `api/main.py`
3. `api/websockets_manager.py`
4. `api/routers/vault.py`
5. `frontend/src/components/Dashboard.jsx`

These five alone reveal the majority of the system's behavior and philosophy.

---

## Part II: Extended Systems Textbook for NetShield

### Chapter 31: Thinking Like a Systems Engineer in This Codebase

When a new engineer first enters this repository, the most important mental shift is to stop reading it as one application and to begin reading it as a federation of cooperating runtimes. The proxy runtime has different constraints, failure modes, and trust boundaries than the API runtime, and the API runtime has different constraints than the frontend runtime. This distinction sounds obvious, but most onboarding mistakes in cross-language systems happen because developers unconsciously assume that all subsystems share the same abstraction level and the same risk profile.

In this project, the C proxy in `proxy/main.c` is much closer to the operating system than the rest of the code. It allocates buffers, copies network bytes, opens sockets, and controls thread synchronization directly. It can fail because of memory mistakes, networking timeouts, DNS lookup failures, partial receive semantics, and race behavior in queueing logic. The FastAPI service in `api/main.py` and `api/routers/*.py` has a completely different shape. It mostly fails at schema boundaries, input validation, JSON parse assumptions, environment configuration mismatches, or long-running task behavior. The React frontend in `frontend/src/components/*.jsx` has yet another shape. It mostly fails because data contracts changed, asynchronous effects race with component lifecycle transitions, or users produce interactions the UI did not anticipate.

The most productive way to work in this repository is to always ask a three-part question for every feature and bug. You should ask where the feature originates, where it is transformed, and where it is observed. If the answer starts at the proxy, transforms in API persistence and background polling, and is observed in the dashboard, you now know that your debugging and design changes are cross-layer by default. If the answer starts in the vault component, transforms in API route orchestration, and relies on subprocess semantics in the C++ binary, you know immediately that you cannot fix it in frontend only. This discipline reduces wasted effort and dramatically improves review quality.

A second key mindset is to separate architectural intent from implementation drift. The intent of NetShield is coherent. The implementation has drifted in places where schema contracts and naming conventions diverged. The right response is not to dismiss the design but to document and repair boundaries with explicit contracts. When reading `schema.sql`, `api/models.py`, `api/threat_updater.py`, and `proxy/main.c`, you can see that multiple iterations of the threat table shape coexist. This is a common state in evolving systems and it is exactly why serious teams formalize migration discipline. Understanding this now will help future contributors turn this project from a strong prototype into a stable platform.

### Chapter 32: Deep Domain Model and Contract Semantics

Every meaningful system has a hidden contract model behind its tables and APIs. In this repository the contract model can be explained in terms of event records, policy records, and asset records. Event records are traffic observations and decisions. Policy records are threat intelligence entries that determine whether a domain should be blocked. Asset records are encrypted file metadata associated with vault workflows. This conceptual split is the most useful one for future maintenance because it clarifies ownership and lifecycle.

Event records are represented by `traffic_logs` in SQL and by `TrafficLog` in `api/models.py`. The contract intention is that every unit of traffic processed by the proxy eventually produces one record with enough context for analytics. That context currently includes domain, method, status code, request and response size, block state, and client address. In a mature production implementation, event contracts also include correlation identifiers, protocol version, latency metrics, decision reason classification, and stable serialization timestamps in UTC with explicit timezone metadata. Even though these are not fully implemented today, understanding this as the natural extension of the current model will guide high quality future development.

Policy records are represented by `threats`. The conceptual contract here is straightforward but operationally sensitive. Every policy record should declare what is blocked, why it is blocked, where it came from, when it became active, and whether it is currently active. The code intent is visible in `api/threat_updater.py` and `api/routers/threats.py`, while the enforcement reference is visible in `proxy/main.c:is_domain_blocked`. A serious edge case appears when field names differ between schema sources. If one component writes `category` and another reads `threat_category`, both modules can be locally correct and globally broken. This is exactly the kind of latent fault that only appears during integrated runtime, not during isolated route testing.

Asset records are represented by `vault_files` and `VaultFile` in ORM. Their conceptual role is to map encrypted file location and retrieval state to a logical record that API and frontend can reason about. In a vault subsystem, metadata quality is as important as encryption quality because policy controls such as one-time retrieval and expiry behavior operate at metadata layer. If metadata is incomplete or inconsistent, the crypto may still be mathematically correct while the security workflow becomes behaviorally weak. For that reason, future contributors should treat metadata fields as security-sensitive contract fields, not as incidental bookkeeping.

### Chapter 33: Expanded Startup and Cold-Boot Behavior

Cold boot is one of the most under-tested states in many repositories, and this project is no exception. The startup sequence has implicit assumptions that deserve explicit documentation because new engineers often run into them during first deployment. The API startup in `api/main.py` calls `init_db()` immediately at module import. This means schema initialization behavior is not delayed until first request. It executes before the app starts serving routes. That design choice is useful because it reduces first-request latency and ensures baseline structures are available early, but it also means startup can fail hard if database paths or mounted schema files are missing.

The startup logic first checks for the DB file and attempts to create the parent directory if necessary. It then looks for schema SQL in `/app/schema.sql` and fallback relative paths. In containerized operation this is controlled by compose volume mount in `docker-compose.yml`. The critical edge case here is when the DB file already exists but has an older or partially incompatible schema. In that case `models.Base.metadata.create_all` will create missing ORM-known tables, but it will not automatically rewrite existing table column definitions. This can produce mixed-schema states that are functional for some flows and broken for others. A robust future direction is to adopt explicit schema migration tooling and run startup compatibility checks that fail with clear diagnostics.

Proxy startup has a different risk profile. The proxy launches thread workers before beginning accept loop, and each worker opens a DB handle. If DB path is wrong, the code still starts in some branches but threat checks become effectively disabled because lookup calls cannot execute successfully. This creates a dangerous silent-degraded state where traffic may be allowed without intended policy enforcement. Production hardening should include mandatory startup validation that exits process if critical DB tables are absent. A strict failure is safer than silent policy bypass.

Frontend startup is usually the easiest but it still has important edge behavior. In development, `frontend/vite.config.js` proxies `/api` calls to local API server. In deployed environments, this assumption may not hold, especially behind reverse proxies or tunneled hostnames. The dashboard websocket URL is assembled from current browser host and protocol, which is convenient but can fail in unusual gateway chains where websocket upgrade paths differ from HTTP route paths. Future hardening can include explicit environment-based API and websocket origin controls with fallback diagnostics shown in UI.

### Chapter 34: Extended Traffic Lifecycle, Including Unhappy Paths

The happy-path request lifecycle has already been described, but robust engineering requires equal focus on unhappy paths. In the proxy, request handling begins when a worker thread receives bytes from client socket. At this stage there are multiple failure possibilities before any policy decision occurs. A client can disconnect early, send malformed request data, send partial headers, or trigger timeout behavior. Each of these must be interpreted correctly to avoid leaking resources and to avoid incorrect logging.

If a request begins with CONNECT, host extraction uses `sscanf` patterns and fallback parsing. Edge cases include host fields with unexpected spacing, missing ports, or appended protocol fragments not expected by parser. The current logic handles many common forms but not all malformed forms. In strict environments you might want to reject ambiguous host syntax early and log parser reason codes. This would produce clearer operational telemetry and reduce attack surface from parser confusion.

If a request is non-CONNECT, parser behavior in `proxy_parse.c` constrains accepted method forms. The parser is oriented around GET semantics. An edge case appears when clients send absolute-form requests with uncommon structures or methods that include request bodies. The current forwarding path does not attempt full HTTP method/body model support. This means compatibility with certain clients and APIs will be limited. That limitation is acceptable in controlled scenarios but should be explicit in product documentation and test matrix.

When remote connection fails, proxy returns errors such as 500 or 502 depending on branch. From a systems perspective it is important to distinguish policy deny from infrastructure failure. A blocked domain should be visible as explicit policy deny, while upstream failure should be visible as network issue. These distinctions influence dashboards, incident triage, and false-positive interpretation. A future analytics enhancement should include a decision_reason field separating threat_block, upstream_unreachable, parse_failure, interception_hold, and normal_allow.

Another unhappy path occurs in interception flow. The proxy reads initial response chunk and inspects headers for content-disposition and MIME hints. If headers are split across packets in unusual ways, initial detection may miss intercept conditions. If binary payload detection fails, the response passes through normally. If detection succeeds but child process or curl command fails, current behavior may still return fallback page semantics that imply secure interception occurred. This can create trust gap unless child success is explicitly verified before user-facing confirmation. A hardened design should propagate child process exit status and include API response checks before declaring successful vault capture.

### Chapter 35: Threat Intelligence Pipeline Under Operational Stress

Threat ingestion logic in `api/threat_updater.py` uses straightforward HTTP fetch and line parsing from two public sources. This simplicity is excellent for readability and educational value, but operational stress introduces conditions that require additional safeguards. Source endpoints may be temporarily unavailable, may throttle clients, may return partial content, or may change format unexpectedly. In the current implementation, broad exceptions are caught and printed, and processing continues with whatever data is available. This behavior keeps the system running but can hide feed quality degradation unless metrics and alerts are layered on top.

A production-grade ingestion design would persist a feed fetch audit record containing source URL, fetch timestamp, response code, content length, parse count, insert count, and checksum fingerprints. That design allows operators to detect sudden drops in imported domain counts that often signal source format changes or outages. In this repository there is a start toward telemetry via `threat_intel_stats`, which records last update time, last added count, and total domains. Extending this into per-source audit would be a natural next step.

Domain normalization is another subtle edge case. Current code strips whitespace and checks comment markers. It also excludes obvious localhost forms. However, domain feeds can include uppercase forms, punycode representations, wildcard prefixes, trailing dots, or malformed labels. The check route in `api/routers/threats.py` includes a regex validator for user-provided domains. Similar normalization standards should be applied consistently during feed ingestion to reduce duplicate or unreachable policy entries.

Concurrency stress is also relevant. The refresh endpoint guards against overlapping update runs with in-memory status lock semantics. This works per process instance, but in scaled multi-instance deployments this lock is not globally shared. If horizontal scaling is ever introduced, a distributed lock or DB-based refresh lease would be required. The current design is correct for single-instance operation and should be documented as such to avoid false assumptions.

### Chapter 36: Vault Workflows, Edge Cases, and Security Correctness

Vault features in this project are one of its strongest differentiators, but they also represent the highest sensitivity area because cryptographic and policy assumptions intersect. The personal upload flow accepts user file and password, writes temporary file, invokes native encryptor in encrypt mode, stores metadata, and deletes temporary cleartext input. This flow is conceptually sound and demonstrates separation between API orchestration and cryptographic implementation. Yet there are still critical edge cases to consider.

The first edge case is parameter handling and password safety. Password values are passed as process arguments when calling the binary. On many systems process arguments can be observable by local administrators or diagnostic tools. For hardened deployment, secrets should flow through secure channels such as stdin, environment variables with strict controls, or IPC mechanisms designed for secret passing. The current approach is practical for prototype and educational contexts, but production hardening should address this explicitly.

The second edge case is decrypt success semantics. The manual decrypt route parses JSON output and validates status, while share download route primarily relies on process return code and does not always parse detailed output equivalently. This asymmetry can permit inconsistent behavior under failure conditions, especially when binary main function returns success code despite internal error status payload. Harmonizing all decrypt code paths to require both non-error exit and explicit `"status":"success"` output would remove ambiguity.

The third edge case is file lifecycle after response generation. Decrypted temp files can persist on disk if not cleaned. This can increase storage pressure and create residual sensitive artifacts. A robust model should use streaming decryption where possible, or immediate cleanup hooks after file response completion. FastAPI supports background tasks and middleware patterns that can help automate cleanup after response close.

One-time retrieval semantics are implemented by marking `is_retrieved` before serving output. This is directionally correct for preventing repeated access, but race conditions can occur if two requests with same valid token arrive nearly simultaneously. In single-threaded low-load contexts this may be rare, but robust implementation should apply atomic DB update semantics or transaction constraints that guarantee only one retrieval can flip state from false to true.

### Chapter 37: Frontend State Management Under Real-World Timing

Frontend code in this project uses localized component state and side effects without global state frameworks. This is often the right choice for medium complexity interfaces because it avoids overengineering while keeping ownership obvious. `Dashboard.jsx` maintains stats, logs, chart data, loading state, and websocket status. The logic combines initial fetch and live stream updates, which is a classic hybrid model for real-time interfaces.

A subtle timing edge case appears when websocket updates arrive before baseline HTTP fetch resolves. Depending on network timing, logs state could receive stream updates and then be overwritten by baseline response payload. In this repository the impact is modest, but in higher load scenarios you may lose early events in view. A robust pattern is to merge baseline and stream state carefully with sequence IDs. Since log IDs are monotonic from DB inserts, deduplication by ID is straightforward.

Another edge case is chart refresh logic. On `update_stats`, the component triggers delayed chart fetch with timeout to absorb backend aggregation timing. This works but can issue overlapping fetches if updates are frequent. Debounce or in-flight request cancellation would improve efficiency. A future refinement can maintain a single scheduled chart refresh or use lightweight incremental aggregation from stream events on client side.

`Threats.jsx` status polling every two seconds is simple and effective for current design. However, if backend is slow or network unstable, repeated polling can stack if responses are delayed. Adding request timeout and skip-if-inflight guard would make behavior more deterministic. You can also transition to websocket notifications for sync status in future if threat pipeline evolves into longer tasks.

`Vault.jsx` includes dual mode interactions and modal password prompts. One edge case is token expiry calculation and timezone assumptions. The component appends `'Z'` to timestamps and compares browser local time against parsed UTC-based values. This is generally fine if backend emits UTC consistently, but mixed-format timestamps can produce false lock states. Standardizing all API timestamps to strict ISO8601 UTC with timezone marker avoids this class of bugs.

### Chapter 38: Performance Engineering Narrative

Performance in this architecture is multi-dimensional. Proxy throughput depends on socket handling and thread scheduling. API responsiveness depends on DB query efficiency and route logic shape. Frontend responsiveness depends on rendering strategy and frequency of state updates. The right performance strategy is never to optimize in one layer blindly. It is to measure where latency or throughput bottleneck actually lives and then optimize the correct boundary.

In proxy layer, worker pool size and queue capacity are primary tuning knobs. The current constants (`NUM_THREADS`, `MAX_CONNECTIONS`) are static. In practical deployment they should be tied to CPU count and expected concurrency model. Too few threads can underutilize hardware. Too many can increase context switch overhead and memory pressure. A measured tuning approach should run synthetic load with mixed CONNECT and non-CONNECT traffic and observe queue depth, request completion latency, and failure ratios.

In API layer, the websocket polling loop runs every second and performs full counts when new logs appear. As traffic volume grows, repeated full-table count can become expensive even with indexes. Improvement path includes incremental counters, materialized stats tables, or periodic batch aggregation. This is a common evolution path for telemetry systems and can be staged without changing frontend contract by preserving response shape.

In frontend layer, rendering long tables and frequent chart updates can stress browser main thread. The dashboard currently caps logs to fifty entries in state, which is good for bounded rendering cost. If future requirements demand longer windows, table virtualization should be introduced. Chart updates can also be throttled under high event rates.

### Chapter 39: Reliability and Fault Tolerance Engineering

Reliability is not only about avoiding crashes. It is about graceful behavior under imperfect conditions. In this project reliability work should target deterministic behavior when dependencies fail. The main dependencies are database file availability, remote threat feeds, remote upstream servers for proxied traffic, and native binary execution for vault operations.

For database reliability, startup checks should verify table presence and required columns before accepting traffic. If schema check fails, process should fail fast with actionable diagnostics. This prevents silent data corruption and partial functionality states that are difficult to detect.

For threat feed reliability, ingestion should preserve previous known-good data on transient failures. Current code already does not wipe table on fetch failure, which is good. Future hardening can track staleness age and expose explicit warning in API status when feed has not updated within expected interval.

For proxy upstream reliability, error responses should encode meaningful distinction between remote DNS failure, connect timeout, and parser failure. This distinction helps operators decide whether issue is policy, network, or code.

For vault reliability, API should validate subprocess outputs robustly and include structured error categories. It should avoid returning success-like responses in ambiguous states. File cleanup routines should run in finally-like patterns wherever temporary files are created.

### Chapter 40: Security Threat Model Expansion

Security review in this repository should start with threat boundaries. The proxy receives untrusted network input. The API receives untrusted HTTP input and file uploads. The frontend executes in user browser and should not be treated as trusted enforcement layer. The encryptor binary executes native code and should be treated as privileged computation unit with strict interface assumptions.

Input injection risks exist in multiple places. In proxy logging function, SQL statements are built with formatted strings. If untrusted fields contain quote characters, SQL injection or malformed inserts become possible. Prepared statements should replace string assembly for all DB writes and reads that include dynamic values.

Shell command construction in interception flow is another serious boundary. Filename extraction from response headers enters command string assembly before execution via shell. Even with quoting attempts, shell parsing complexity can open command injection vectors. The safer pattern is to avoid shell entirely and execute command with fixed argument array, passing filename as data argument without shell interpretation.

Authentication and authorization are currently minimal across management endpoints. In real environments, endpoints that modify threat policy or decrypt assets must require authenticated principal identity and role controls. Audit logging should include actor identity and action context.

Secret management should be strengthened. Environment variable naming should be consistent. Secrets should not have insecure defaults in production. Local development defaults are acceptable if explicitly marked and blocked in non-development mode.

### Chapter 41: Future Development Blueprint, Phase by Phase

Future development can be organized into controlled phases that each produce measurable gains. The first phase should be contract stabilization. This phase unifies schema and model names, introduces migration files, and adds startup schema validator. At completion, every table and column used by proxy and API is formally versioned.

The second phase should be security hardening. This phase replaces SQL string interpolation with prepared statements, removes shell command execution paths for dynamic data, introduces API auth and authorization, and standardizes secret configuration. At completion, the system can be considered safer for controlled deployment.

The third phase should be reliability and observability uplift. This phase adds structured logs, correlation IDs, per-source threat ingestion metrics, and health endpoints. It also introduces cleanup guarantees for temporary decrypted files and robust subprocess status checks. At completion, operational support and incident diagnosis become significantly easier.

The fourth phase should be scalability uplift. This phase moves from SQLite to server-grade database if needed, introduces event-driven telemetry pipeline instead of polling for high-throughput deployments, and expands proxy protocol support while preserving policy behavior.

The fifth phase should be productization and developer experience. This phase includes automated tests, CI workflows, linting and static analysis across languages, architecture diagrams generated from code contracts, and contributor templates for new endpoints and data fields.

### Chapter 42: Edge Cases Catalog for Maintainers

Edge case one concerns schema drift over time. A developer may run with old DB file from previous branch while code expects new columns. The app can partially start and fail later in routes or proxy checks. A migration checksum mechanism and startup gate would catch this immediately.

Edge case two concerns domain canonicalization. A domain can appear with uppercase letters, trailing dots, or punycode. Policy checks should normalize these forms consistently before lookup. Without normalization, duplicate entries and missed matches occur.

Edge case three concerns long-lived websocket connections across frontend tab inactivity and browser sleep. Reconnection logic should handle stale sockets and network transitions gracefully. The current reconnect behavior is useful but can be refined with exponential backoff and visibility state awareness.

Edge case four concerns concurrent retrieval of one-time share links. Two requests may pass validation before one updates retrieval state. Atomic update semantics are required to guarantee strict one-time behavior.

Edge case five concerns intercepted payloads where header hints are absent but content is still sensitive. Current interception heuristics are header-based and may miss some payloads. Future development can include optional content signature checks and configurable interception policy sets.

Edge case six concerns large file uploads and memory pressure. API currently reads uploaded files into memory before writing in certain paths. For very large files, streaming upload handling is more stable and reduces peak memory use.

Edge case seven concerns timezone parsing differences in frontend and backend. Timestamp formatting consistency must be strict to avoid false expiration and chart bucketing anomalies.

Edge case eight concerns DNS failures and retries in proxy remote connections. Without controlled retry strategy and reason logging, transient DNS faults can look like policy denials to operators.

Edge case nine concerns partial packet headers in proxy interception detection. Assuming full headers in first receive can fail for fragmented responses. Header accumulation logic should be robust.

Edge case ten concerns threat feed poisoning and malformed feed lines. Ingestion should validate domain tokens conservatively and optionally verify source integrity via signatures or pinned checksums if available.

### Chapter 43: Comprehensive Refactoring Guidance

Refactoring this project should be contract-first rather than style-first. Many teams begin by reorganizing folders or renaming files and only later discover behavioral contract mismatches remain unsolved. In NetShield, the first refactor target should be a unified data contract document generated from code and schema, followed by test cases that enforce contract.

In proxy module, refactoring should focus on extracting explicit units for parsing, policy decision, forwarding, interception, and persistence. This allows isolated tests and easier reason-based logging. It also reduces coupling where one function currently performs too many responsibilities.

In API module, refactoring should focus on standardizing data access patterns. Some routes use ORM while others use raw sqlite3. Choosing one primary pattern per bounded context improves maintainability. A practical compromise is to retain raw SQL only for specific high-performance or compatibility cases and document those exceptions.

In frontend module, refactoring should focus on shared API client utilities, typed response normalization, and reusable hooks for polling and websocket patterns. This reduces repeated async boilerplate and improves consistency in error handling.

In vault subsystem, refactoring should define a strict command interface contract for encryptor binary outputs and exit codes. API should treat command execution as protocol, not ad hoc subprocess call. This means versioned command modes, stable JSON schema, explicit error codes, and compatibility tests.

### Chapter 44: Extended Code Reading Companion

If you open `proxy/main.c`, read it in four passes. In pass one, only trace connection lifecycle from accept to close. In pass two, isolate policy checks and logging behavior. In pass three, isolate interception flow and child process behavior. In pass four, annotate every external side effect including DB writes, socket sends, and subprocess execution.

If you open `api/routers/vault.py`, read it in three passes. In pass one, map each endpoint and required inputs. In pass two, map every filesystem operation and subprocess call. In pass three, map every failure branch and status code returned to caller. This method quickly reveals where consistency and cleanup improvements are needed.

If you open `api/encryptor/main.cpp`, read it as two distinct subsystems. One subsystem is file cryptography and key derivation. The other subsystem is token signing and verification. Keeping these conceptual lanes separate prevents confusion because they share utility functions but solve different security problems.

If you open `frontend/src/components/Dashboard.jsx`, read it as asynchronous state machine rather than static UI code. The component transitions through loading, connected, disconnected, and live-update modes. This framing helps you reason about race conditions and user-visible stale states.

### Chapter 45: Long-Term Architectural Evolution

In the long term, NetShield can evolve into a modular security platform with formal plugins for policy engines, feed sources, and vault providers. The current architecture already hints at this possibility because concerns are separated by runtime boundaries. A plugin-ready roadmap would define stable interfaces for policy check modules in proxy, feed ingestion modules in API, and retrieval policy modules in vault subsystem.

A future event pipeline can replace DB polling with append-only event stream semantics. The proxy can emit structured events into queue or stream, API can consume and materialize aggregates, and frontend can subscribe through websocket gateway with richer filtering. This model improves scalability while preserving current operator experience.

Policy model can evolve from domain-level blocking to richer context-aware decisions that include destination IP reputation, request method sensitivity, user identity context, and temporal anomaly scoring. This would require redesigned schema and decision APIs, but the foundational pattern already exists in this repository.

Vault model can evolve into configurable retention and legal hold workflows where encrypted assets have policy-driven lifecycle controls. This is especially valuable if the system is used for malware analysis pipelines where payload provenance and chain-of-custody metadata are important.

Frontend can evolve from tabbed dashboard into role-based control center with incident workflows, queued investigations, and policy diff previews. This evolution should remain contract-driven so that UI capabilities map cleanly to auditable API actions.

### Closing Section of Part II

This extended volume exists to make the project teachable at engineering depth, not merely demonstrable at demo depth. If you study the chapters above with the referenced files open side by side, you will understand not only what the system does today, but also how to evolve it safely. You will understand where abstractions are strong, where boundaries need reinforcement, and where edge cases hide behind happy-path behavior. That is the difference between reading code and owning a system.

---

## Part III: Implementation Masterclass and Operational Handbook

### Chapter 46: Contracts Before Code Changes

In mature engineering teams, the most expensive bug category is contract violation, not syntax error. A syntax error fails fast and visibly. A contract violation can survive tests and fail only under production shape traffic. In this repository, contract boundaries are explicit in five places. The first boundary is data schema in `schema.sql` and `api/models.py`. The second boundary is route response shape in `api/routers/*.py` and frontend components. The third boundary is subprocess protocol between `api/routers/vault.py` and `api/encryptor/main.cpp`. The fourth boundary is policy lookup behavior in `proxy/main.c` against threat table semantics. The fifth boundary is timestamp and timezone formatting between backend and UI.

Before adding features, developers should write a short contract note that states what field names, value domains, and invariants are expected across all touched modules. For example, if a new threat source confidence field is introduced, that note should define storage type, nullable policy, default value behavior, API serialization representation, and frontend rendering expectations. Without this note, one layer may treat missing confidence as zero while another treats it as unknown, and operators may receive misleading visualizations. It is better to write one page of contract definition than to debug one week of inconsistent behavior.

A practical process that works well in this codebase is contract-first pull requests. First PR introduces schema and model contracts with no behavioral changes. Second PR adapts writers. Third PR adapts readers. Fourth PR enables UI rendering. This staged approach makes reviews cleaner and allows rollback without full feature reversal. Even in small teams, this structure pays off because mixed language stacks amplify hidden coupling.

### Chapter 47: Advanced Logging and Traceability Design

The current codebase logs useful events but does not yet provide unified structured observability. In systems that cross C, Python, and JavaScript, traceability requires one shared event identity concept. A future enhancement should assign a request correlation ID at proxy ingress and carry that ID into traffic logs, API websocket payloads, and frontend visible records. This improves debugging because engineers can follow one event through all layers.

In proxy layer, structured logging can be introduced by emitting JSON-like lines to stdout with fixed keys such as timestamp, request_id, host, method, decision, status_code, and error_reason. In API layer, route middleware can enrich logs with request path, latency, and user context when auth is introduced. In frontend layer, development mode can log state transitions keyed by request_id to detect race and rendering anomalies.

Another useful pattern is explicit audit streams for policy changes. When a domain is added manually via `api/routers/threats.py`, an audit record should include actor identity, old state, new state, source metadata, and reason text. This becomes critical once the project is used by more than one operator because policy updates become collaborative and potentially contentious.

### Chapter 48: Data Integrity and Idempotency

Idempotency is a foundational property in distributed and asynchronous systems. In NetShield, ingestion and metadata updates should be designed so repeated operations do not produce harmful duplicates. The updater already uses `INSERT OR IGNORE` patterns, which is a strong start. Similar principles should be applied to vault metadata writes and one-time state transitions.

One-time retrieval flag updates should use atomic compare-and-set behavior at database layer. A safe pattern is an update statement that sets `is_retrieved = true` only where `id = ?` and `is_retrieved = false`, then checks affected row count. If row count is zero, retrieval should fail because another process already consumed it. This pattern eliminates races where two requests validate token and both proceed.

In traffic logging, idempotency is harder because each network event is naturally unique. However, if retries or duplicated connection handling can occur under edge conditions, adding optional deduplication keys such as tuple of timestamp bucket, client endpoint, method, and host hash can help for offline analytics. This should be optional because strict dedupe in real-time path may hide true repeated attempts.

### Chapter 49: Comprehensive Error Taxonomy

A robust system should classify errors by domain rather than by ad hoc status messages. A recommended taxonomy for this project includes policy errors, parser errors, network errors, storage errors, crypto errors, and contract errors. Policy errors correspond to intentional denies, such as blocked domains. Parser errors correspond to malformed requests and unsupported protocol forms. Network errors correspond to DNS and connect failures. Storage errors correspond to DB unavailable, missing tables, and transaction failures. Crypto errors correspond to invalid password, authentication tag failures, and token signature mismatch. Contract errors correspond to serialization mismatches and missing expected fields between components.

The reason this taxonomy matters is operational triage. If all failures appear as generic server errors, operators cannot prioritize corrective actions correctly. A parser error burst may indicate malformed traffic or attack probing. A storage error burst indicates infrastructure risk. A crypto error burst during vault downloads may indicate user credential mismatch or corruption. The system should expose reason classes in logs and optionally in API responses where safe.

### Chapter 50: Schema Migration Strategy in Detail

Schema migration should be explicit and versioned. A practical route for this repository is to create a migrations directory and a schema_version table. Every migration script should have monotonic numeric prefix and clear rollback notes. Startup should read schema_version and refuse to run when binary or code expects a newer version than DB has, unless auto-migration is configured.

A migration discipline for this project should include preflight checks that confirm required indexes exist for frequently queried fields such as traffic timestamp and blocked flag. Since dashboard and polling rely on these patterns, missing indexes can cause performance cliffs that look like random slowdown.

When changing threat table columns, migration scripts should preserve backward compatibility window. For example, if moving from `threat_category` to `category`, create target column, backfill from source, update writers and readers in phased deployment, and only then remove old column. Direct in-place rename assumptions often fail across mixed runtime versions.

### Chapter 51: Memory Safety and C Layer Hardening

C code in proxy is one of the strongest educational parts of this project because it exposes real systems concerns. It is also where memory and string safety must be treated rigorously. Buffer sizes and copy semantics should be audited for every path where untrusted input influences string operations. For example, `snprintf` use is safer than `sprintf`, but SQL string assembly still remains vulnerable to semantic injection even when buffer overflow is prevented.

Where possible, use prepared statements for DB writes and avoid hand-built SQL strings. For parser structures, ensure clear ownership and lifecycle around allocations in `proxy_parse.c`. If parse fails mid-way, cleanup paths should release all allocated resources. Static analyzers and sanitizers should be integrated into build workflow. AddressSanitizer runs in development can catch hidden issues before they become runtime instability.

Thread safety should also be reviewed with explicit lock boundaries. Queue and cache or other shared structures must avoid lock inversion patterns. Any future introduction of additional shared maps for policy caches should include lock-order documentation to prevent deadlocks.

### Chapter 52: API Robustness and Validation Framework

FastAPI routes currently perform direct logic and DB access in route functions. This is acceptable for medium scope, but as complexity grows, service-layer extraction improves testability. A good future architecture is route layer for validation and response mapping, service layer for business logic, and repository layer for storage operations. This pattern allows mocking at boundaries and keeps route handlers concise.

Validation should standardize around shared utilities for domain normalization, token parsing, and timestamp handling. If different routes each implement their own normalization, subtle divergence appears over time. The domain regex in threats route is valuable, and equivalent normalization should be used in updater and potentially proxy checks for consistency.

Error responses should become typed with machine-readable codes alongside human messages. This helps frontend provide context-sensitive messages and allows automation to react intelligently. For example, token-expired and token-invalid are both access errors but should not be treated identically by UI.

### Chapter 53: Frontend UX Resilience and Operator Trust

In security dashboards, operator trust is built on consistency and explainability. If numbers jump without context or statuses flicker unpredictably, users lose confidence in system accuracy. Frontend should therefore emphasize stable transitions, explicit loading states, and confidence cues tied to backend freshness.

For dashboard, showing last websocket heartbeat and last data refresh time can reassure users that values are current. For threats panel, showing per-source fetch outcomes with timestamps helps operators understand whether current policy set is fresh or stale. For vault panel, explicit status labels for encryption success, token generation success, retrieval consumed, and expiry elapsed reduce ambiguity.

Accessibility and readability also matter. A highly animated UI can look impressive but should not obscure state semantics. Provide clear text fallback for status colors and ensure critical security states are not color-only indicators.

### Chapter 54: Cryptography Correctness and Practical Security

The C++ crypto module demonstrates strong primitive choices, but practical security depends on surrounding workflow. Key derivation iterations are high, which is good for password hardening. Random salt and IV handling are present, and GCM authentication tag verification is implemented, which is essential. However, practical security also needs careful secret lifecycle handling and secure default operational posture.

Secrets should be loaded from environment with strong defaults disabled in production. Master password values should not be static placeholders beyond local demo contexts. Token signature secret should be rotated according to operational policy. If secret rotation is introduced, token verification should support key versioning for transition windows.

Another practical recommendation is to include authenticated metadata if certain file properties must be protected against substitution. AES-GCM supports additional authenticated data flows. If future design needs to bind file identifiers or policy claims to ciphertext integrity, this can be implemented.

### Chapter 55: Threat Intelligence Quality Assurance

Threat feeds vary in reliability and scope. Operational quality improves when ingestion pipeline scores source quality and tracks trend lines. Sudden source volume drops, repeated parse failures, or unusual duplicate ratios should produce warnings. Threat systems should treat feed health as first-class signal, not as background detail.

A valuable enhancement is to store source-specific last successful update timestamps and parse counts. This enables UI to show whether one source is stale while others are current. Operators can then avoid false confidence from aggregate counts that hide partial source outages.

Testing feed parser against canned fixture files is also important. If upstream format changes unexpectedly, fixture-based tests can fail early in CI. This protects runtime systems from silent breakage.

### Chapter 56: Advanced Testing Architecture

Testing in cross-language systems should be layered. Unit tests validate local behavior. Integration tests validate boundaries. End-to-end tests validate operator outcomes. For this repository, unit tests in Python should cover route-level behaviors and service utilities. C layer can use targeted functional tests for parser and decision logic with controlled input fixtures. Frontend tests should validate render and interaction flows with mocked API contracts.

Integration tests should run composed services and verify key lifecycles such as blocked domain path, threat refresh propagation, and vault share consumption. End-to-end tests should run from browser perspective and assert user-visible outcomes. A failing integration test should point to contract mismatch, while failing unit test should point to local behavior regression.

Performance tests should also be included. At minimum, establish baseline throughput and latency for proxy under mixed request types, API stats route under growing traffic logs, and websocket update delivery under burst conditions.

### Chapter 57: Deployment Topologies and Environment Profiles

Current deployment model is single compose stack, which is ideal for development and demos. For staging and production-like setups, profile-based configuration is necessary. Development profile can enable autoreload and permissive settings. Staging profile should enable stricter secrets and stability settings. Production profile should enable strict auth, hardened CORS, and detailed observability.

When deploying behind external reverse proxies, websocket route forwarding and timeout tuning should be verified. Some reverse proxies need explicit upgrade headers and idle timeout adjustments for stable websocket behavior.

Persistent volume management should include backup and restore strategy for DB and vault storage. If one-time links are operationally important, metadata backup consistency with encrypted file storage must be maintained to avoid orphaned records or files.

### Chapter 58: Incident Response Playbook Concepts

An incident response model for this project should define triage pathways for three major incident classes. The first class is policy bypass incidents where blocked domains were unexpectedly allowed. The second class is data integrity incidents where analytics counters disagree with raw logs. The third class is vault incidents where encrypted assets cannot be retrieved or were retrieved unexpectedly.

For policy bypass, first check threat table state and column contracts, then check proxy lookup query behavior, then inspect runtime logs for DB open errors. For data integrity, compare direct DB counts with API route outputs and websocket payload deltas. For vault incidents, verify token validity, retrieval flags, decrypt output statuses, and file presence in storage paths.

Incident records should preserve exact build version and schema version. Without this, postmortem analysis is often inconclusive in evolving codebases.

### Chapter 59: Documentation as a Living System

This manual should be treated as a maintained subsystem, not static prose. Every significant contract change should update reference sections in this document. The fastest way for documentation to become untrusted is to lag behind implementation behavior. Therefore future PR templates should include a check asking whether this manual needs update for changed behavior.

A practical convention is to embed short "reference anchors" in code comments that map to manual chapter numbers for complex logic areas. For example, interception logic in `proxy/main.c` can include a comment pointing to manual chapter describing security and failure behavior. This helps future readers bridge code and rationale quickly.

### Chapter 60: Concluding Perspective for Future Builders

NetShield already demonstrates rare breadth for one repository. It unifies systems programming, backend API design, frontend telemetry UX, and native cryptographic workflows in a single coherent architecture. That breadth is valuable, but long-term excellence comes from disciplined contract management, security hardening, testing depth, and operational clarity.

If you continue this project, do not lose the educational clarity that makes it approachable today. Instead, build rigor around the existing strengths. Preserve clear module ownership. Keep cross-language contracts explicit. Expand edge-case handling deliberately. Add observability before complexity. Build trust through deterministic behavior.

When done correctly, this repository can evolve from a strong project into a reusable security platform and teaching benchmark for full-stack systems engineering.




Your task is to fully analyze and explain this entire project. Do NOT give a shallow summary — I need a structured, detailed breakdown covering architecture, data flow, and design decisions.



Follow this exact structure:



---



## 1. Project Overview (High-Level)

- What problem does this project solve?

- Why would someone build this system?

- What are the core objectives and use-cases?

- Who are the intended users (if applicable)?



---



## 2. Tech Stack Identification

Scan the entire repository and list:

- Programming languages used

- Frameworks/libraries

- Tools and external dependencies

- Protocols (HTTP, TCP, etc.)

- Any system-level concepts (multithreading, IPC, etc.)



For each item:

- Explain WHY it is used in this project (not just what it is)



---



## 3. System Architecture (VERY IMPORTANT)

- Break the system into major components/modules

- Explain responsibility of each module

- Describe how modules interact with each other

- Identify entry points (main function, server start, etc.)



If possible:

- Explain architecture in layers (e.g., client → server → processing → storage)



---



## 4. Folder & Codebase Structure

- Walk through the folder structure

- Explain purpose of each important directory/file

- Identify “critical files” that drive the system



---



## 5. End-to-End Data Flow (CRITICAL)

Explain step-by-step:



- When a request/input enters the system:

  1. Where does it enter?

  2. Which files/functions handle it first?

  3. How does data move across modules?

  4. Where is transformation happening?

  5. Where is output generated?



- Trace a COMPLETE lifecycle of data through the system



---



## 6. Core Logic & Key Implementations

- Identify most important algorithms / logic

- Explain how they work (step-by-step)

- Highlight any optimizations (multithreading, caching, etc.)



---



## 7. Design Decisions & Tradeoffs

- Why this approach was chosen over alternatives

- Any constraints (performance, security, memory, etc.)

- Trade-offs made in implementation



---



## 8. Strengths of the Project

- What makes this project technically strong?

- What would impress a recruiter or engineer?



---



## 9. Weaknesses / Possible Improvements

- What can be improved?

- Missing features, scalability issues, etc.



---



## 10. Resume-Ready Bullet Points

Convert this entire project into 3–4 strong, ATS-friendly resume bullet points:

- Focus on impact + technical depth + keywords



---



IMPORTANT INSTRUCTIONS:

- Be specific. Refer to actual files/functions wherever possible.

- Do NOT give generic explanations.

- Think like someone reverse-engineering the system.

- If something is unclear, infer logically from code patterns.

- Prioritize clarity and depth over brevity.



---

1. Project Overview (High-Level)
What problem does this project solve?
It provides a “gateway” that (a) blocks requests to known malicious domains using a live SQLite-backed threat feed and (b) streams traffic analytics to a browser UI in near real time via WebSockets. It also includes a secure encrypted “vault” workflow for storing and one-time downloading intercepted or user-uploaded files.
Why would someone build this system?
To combine lightweight network enforcement (a C TCP proxy) with operational visibility (FastAPI + WebSocket streaming + React dashboard), plus an encrypted storage/sharing mechanism (C++ AES-256-GCM engine exposed through the API).
What are the core objectives and use-cases?
Real-time traffic monitoring (api/routers/traffic.py + api/websockets_manager.py).
Domain-based blocking using threat intelligence (api/threat_updater.py populates SQLite; proxy/main.c checks threats during requests).
Secure vault encryption/decryption and share links (api/routers/vault.py shells out to api/encryptor/main.cpp).
Optional “MITM-style” interception of downloadable/binary responses that are routed into the vault.
Who are the intended users (if applicable)?
Someone running this as a local/security lab gateway: operators who want a dashboard for blocked/allowed traffic and a secure encrypted “vault” for payloads.
2. Tech Stack Identification
Python + FastAPI (api/main.py, api/routers/*.py)
Why: REST endpoints and a WebSocket endpoint (@router.websocket("/live")) for delivering traffic logs and stats to the frontend.
WebSockets (api/websockets_manager.py, websockets dependency)
Why: the dashboard needs push-based updates for new traffic_logs rows without repeated polling from the browser.
SQLAlchemy + SQLite (api/database.py, api/models.py, schema.sql)
Why: shared persistence between the C proxy and the API via a shared mounted SQLite file (docker-compose.yml mounts netshield-data:/data and uses DB_PATH=/data/netshield.db).
Pydantic (api/schemas.py)
Why: serializable response models for traffic logs (not heavily used for vault).
Requests (api/threat_updater.py)
Why: fetches external blocklists (StevenBlack hosts + URLHaus hostfile) and writes them into SQLite.
React + Vite + JSX (frontend/src/*, frontend/vite.config.js)
Why: the UI renders a live dashboard and provides UX for threat refresh/checking and the encrypted vault.
Recharts + framer-motion + lucide-react (frontend/package.json)
Why: charting (24-hour aggregation), animated UI, and iconography.
C + pthreads + sockets + select (proxy/main.c)
Why: the enforcement layer must be a fast TCP proxy; it uses a worker thread pool (NUM_THREADS=20) and multiplexing (select) for CONNECT tunnels.
SQLite C API (proxy/main.c uses sqlite3_*)
Why: the proxy needs to synchronously check threat membership and write traffic_logs rows.
C++ + OpenSSL (api/encryptor/main.cpp)
Why: production vault crypto (AES-256-GCM + PBKDF2 key derivation + HMAC-SHA256 token signing/verification).
Protocols / system concepts
HTTP/HTTPS (proxy parses GET-style absolute-form request lines and CONNECT tunneling).
WebSocket (FastAPI websocket endpoint + browser WS client).
TCP sockets (proxy server accept/connect/relay).
Multithreading (C proxy worker pool).
IPC via fork() + pipe() (proxy interception uses a child sh -c curl ... subprocess).
3. System Architecture (VERY IMPORTANT)
Major components/modules
Enforcement / traffic interception: proxy/main.c
Responsibilities: accept client TCP connections, parse requests, query threats table, enforce blocking, log to traffic_logs, and optionally intercept binary/attachment responses into the vault by calling the API.
Backend API: api/main.py + api/routers/*
Responsibilities: expose traffic analytics endpoints, expose threats management endpoints, and expose vault encryption/decryption endpoints.
WebSocket broadcasting loop: api/websockets_manager.py
Responsibilities: background polling of SQLite for new traffic_logs and broadcasting new_log + update_stats messages.
Threat feed ingestion: api/threat_updater.py
Responsibilities: download threat source lists and insert domains into SQLite threats.
Crypto engine: api/encryptor/main.cpp (compiled binary in the container)
Responsibilities: AES-256-GCM encryption/decryption and HMAC-signed token generation/verification.
Frontend UI: frontend/src/main.jsx, frontend/src/App.jsx, and components
Responsibilities: fetch baseline dashboard data, connect WebSocket for live updates, render threats/vault pages, and upload/download encrypted content.
How modules interact
Shared storage: docker-compose.yml mounts the same SQLite file into both proxy and api containers using netshield-data:/data and DB_PATH.
Traffic flow coupling:
proxy/main.c writes traffic_logs.
api/websockets_manager.py polls traffic_logs and broadcasts updates.
frontend/src/components/Dashboard.jsx consumes WebSocket messages and renders a rolling table + refreshes the 24-hour chart using GET /api/traffic/chart.
Threat flow coupling:
frontend/src/components/Threats.jsx triggers POST /api/threats/refresh.
api/threats.py runs api/threat_updater.py:update_threats() to insert into threats.
Subsequent requests processed by proxy/main.c:is_domain_blocked() consult the updated threats.
Vault flow coupling:
frontend/src/components/Vault.jsx uploads to POST /api/vault/upload or POST /api/vault/share/generate, and downloads via POST /api/vault/decrypt or GET /api/vault/share/download/{token}.
proxy/main.c “intercepts” certain responses and calls the same api endpoint (/api/vault/share/generate) via curl inside a forked child.
Entry points
Proxy: proxy/main.c:main() binds PROXY_PORT (default 8888) and starts the accept loop + worker pool.
API: api/Dockerfile runs uvicorn main:app; the FastAPI app is created in api/main.py.
API initialization: api/main.py:init_db() optionally executes /app/schema.sql, then calls models.Base.metadata.create_all(...).
WebSocket background task: api/main.py startup event runs asyncio.create_task(poll_database_for_updates()).
Vault crypto entry: api/encryptor/main.cpp:main() multiplexes modes (encrypt, decrypt, sign_token, verify_token).
Frontend: frontend/src/main.jsx renders frontend/src/App.jsx.
4. Folder & Codebase Structure
Top-level orchestration
docker-compose.yml
Builds and wires proxy, api, and frontend, shares volumes (netshield-data, vault-data), and mounts schema.sql into the API container.
Makefile
Wraps docker-compose build/up/down/reset.
schema.sql
Defines SQLite tables (traffic_logs, threats, vault_files, system_stats).
Backend (api/)
api/main.py
FastAPI app creation + init_db() + startup task for websocket polling.
api/database.py
SQLAlchemy engine/session configuration for SQLite with check_same_thread=False.
api/models.py
ORM models for TrafficLog, SystemStat, Threat, ThreatIntelligenceStat, VaultFile.
api/websockets_manager.py
ConnectionManager and poll_database_for_updates() which polls new traffic_logs rows.
api/threat_updater.py
update_threats() fetches remote lists and inserts into SQLite.
api/routers/
traffic.py: /api/traffic/logs, /api/traffic/stats, /api/traffic/chart, websocket /api/traffic/live.
threats.py: /api/threats/refresh, /status, /stats, /recent, /check, /add.
vault.py: /api/vault/upload, /share/generate, /share/download/{token}, /files, /decrypt.
api/encryptor/
main.cpp: AES-GCM + PBKDF2 + HMAC token engine.
Makefile: builds the encryptor binary inside the API container.
Enforcement layer (proxy/)
proxy/main.c
Thread pool, request handling, threat checks, traffic logging, and optional interception to vault share generation.
proxy/proxy_parse.c + proxy/proxy_parse.h
HTTP request parsing/unparsing utilities used by the proxy for GET request forwarding.
proxy/Dockerfile, proxy/Makefile
Alpine build and compile linking against libsqlite3.
Frontend (frontend/)
frontend/vite.config.js
Dev server proxying /api to the backend and WebSocket forwarding.
frontend/src/
main.jsx: bootstraps React.
App.jsx: renders tabs (dashboard/threats/vault).
components/Dashboard.jsx: baseline fetch + websocket subscription.
components/Threats.jsx: threat refresh/check/add UI.
components/Vault.jsx: encryption upload and one-time download workflow.
Additional projects included in repo
encrypty/
Standalone C++ demo: uses a fork/queue model (ProcessManagement) and a simple file “encrypt/decrypt” (byte shift) driven by .env key; not the production vault crypto.
Proxy server/
Alternate C proxy with LRU caching (GET-only). It is not the main containerized proxy used by docker-compose.yml.
5. End-to-End Data Flow (CRITICAL)
When a request/input enters the system
Where does it enter?
Network traffic enters via the C proxy container port PROXY_PORT (default 8888) defined in docker-compose.yml and implemented in proxy/main.c:main().
Which files/functions handle it first?
proxy/main.c:main() sets up the accept loop and enqueues accepted sockets.
proxy/main.c:worker_thread() dequeues a client socket and reads the request bytes.
How does data move across modules?
Proxy consults SQLite threats (same DB file shared with API).
Proxy writes an analytics row into SQLite traffic_logs.
API reads new rows via api/websockets_manager.py:poll_database_for_updates() and broadcasts websocket payloads.
Frontend consumes websocket payloads in frontend/src/components/Dashboard.jsx.
Where is transformation happening?
Proxy transforms network inputs into DB rows (proxy/main.c:log_to_db()), including mapping blocked/enforced decisions to the blocked column.
API transforms ORM model objects into JSON payloads for websocket messages (api/websockets_manager.py:poll_database_for_updates() builds log_dict).
Frontend transforms timestamps to local display in the table render (new Date(log.timestamp...) in Dashboard.jsx).
Where is output generated?
DB output: traffic_logs rows via INSERT INTO traffic_logs ... in proxy/main.c:log_to_db().
UI output: live table updates from websocket messages; chart output from GET /api/traffic/chart aggregation.
Complete lifecycle trace (traffic analytics)
Browser loads Dashboard.jsx:
Fetches baseline analytics in parallel:
fetch('/api/traffic/stats')
fetch('/api/traffic/logs?limit=50')
fetch('/api/traffic/chart')
Connects WebSocket to /api/traffic/live.
A client makes a request to the proxy:
proxy/main.c:worker_thread() reads from the socket.
If it sees CONNECT, it extracts host and checks is_domain_blocked(db, host):
If blocked: sends HTTP/1.1 403 Forbidden to the client and logs blocked=1.
If allowed: connects to remote server and relays bytes; logs blocked=0.
If it’s a non-CONNECT request, it parses the request via ParsedRequest_parse() (GET-only) and checks is_domain_blocked(db, req->host):
If blocked: sends 403 and logs blocked=1.
If allowed: forwards the request and reads the response; then inspects response headers for interception triggers.
Proxy writes analytics:
proxy/main.c:log_to_db() inserts a row into SQLite traffic_logs with fields like domain, method, status_code, request_size, response_size, blocked, client_ip.
API websocket broadcaster detects new logs:
api/websockets_manager.py:poll_database_for_updates() runs in a loop:
Queries traffic_logs where id > last_seen_id.
For each row, creates log_dict and broadcasts {"type":"new_log","data":...}.
Then recomputes counts and broadcasts {"type":"update_stats","data":...}.
Browser UI updates:
On new_log, Dashboard.jsx prepends the new record and truncates to the latest 50 entries.
On update_stats, it updates the stat counters and triggers a delayed GET /api/traffic/chart to refresh hourly aggregation.
6. Core Logic & Key Implementations
Threat blocking logic (domain → block decision)
Implemented in proxy/main.c:is_domain_blocked(sqlite3 *db, const char *domain)
It strips a leading www. when checking subdomain variants.
It executes a prepared statement:
SELECT id FROM threats WHERE (domain = ? OR domain = ?) AND is_active = 1 ...
Returns 1 (blocked) if a match exists; otherwise 0.
Traffic logging (what gets recorded)
Implemented in proxy/main.c:log_to_db(...)
Builds and runs an INSERT INTO traffic_logs ... string via snprintf.
Writes blocked plus client_ip, timestamp via CURRENT_TIMESTAMP.
Concurrency + server behavior
proxy/main.c uses:
A bounded socket queue with pthread_mutex_t + pthread_cond_t.
A fixed worker pool (NUM_THREADS=20).
For HTTPS relaying, handle_https() uses select() to forward between client and remote sockets.
HTTP parsing and forwarding
Non-CONNECT requests use the included parser:
proxy_parse.c provides ParsedRequest_parse(), ParsedRequest_unparse(), and header manipulation helpers.
proxy/main.c explicitly handles GET requests (parser rejects other methods).
Response interception → vault share generation (MITM-style vaulting)
In proxy/main.c, after receiving a remote response:
It parses response headers from the initial recv() buffer.
Sets should_intercept if:
Content-Disposition includes attachment, and extracts filename=... into intercept_filename, OR
Content-Type indicates binary downloads (application/octet-stream, application/x-msdownload, application/zip).
If intercepting:
Uses fork() + pipe() to stream the intercepted response body into a child subprocess that runs:
curl -X POST http://api:8000/api/vault/share/generate -F 'file=@-;filename="<intercept_filename>"' -F 'expiry_hours=24'
Sends a fallback HTML page to the client indicating vault interception.
Cryptography: AES-256-GCM + PBKDF2 key derivation
Implemented in api/encryptor/main.cpp
do_encrypt(istream& in, ostream& out, const string& password, vector<unsigned char>& out_salt)
Generates random salt (32 bytes) and random iv (12 bytes).
Derives an AES key via PKCS5_PBKDF2_HMAC(..., PBKDF2_ITERATIONS=310000, EVP_sha256(), 32 bytes).
Encrypts using EVP_aes_256_gcm() in streaming mode.
Appends the GCM tag to the end of the output stream.
Writes file format as: [salt][iv][ciphertext...][tag].
Wipes the derived key using OPENSSL_cleanse(...).
do_decrypt(...)
Reads salt and iv from the file header.
Derives the same key and decrypts ciphertext bytes.
Verifies tag via EVP_DecryptFinal_ex; fails return causes ciphertext output removal.
“JWT-like” share tokens (HMAC-SHA256)
Implemented in api/encryptor/main.cpp modes:
sign_token:
Builds header and payload JSON strings.
Encodes with base64url_encode.
Signs with hmac_sha256(header.payload, secret) and base64url encodes the signature.
Output format: header.payload.signature.
verify_token:
Splits the token at . delimiters.
Recomputes expected HMAC and compares signature.
Extracts file_id and exp from decoded payload and checks current time.
Vault one-time download enforcement
Implemented in api/routers/vault.py:share_download(token: str, db: Session = Depends(get_db))
Verifies token using encryptor verify_token.
Loads models.VaultFile by file_id.
Enforces one-time retrieval by checking/setting db_file.is_retrieved.
7. Design Decisions & Tradeoffs
SQLite shared state (proxy + API + UI)
Why chosen: simple deployment and direct cross-language integration (C writes rows; Python reads rows; React consumes via API).
Tradeoff: concurrency/schemas must be consistent across components; the current repo has schema/column-name mismatches (see weaknesses).
Polling SQLite for WebSocket updates
Why chosen: avoids relying on SQLite triggers/notifications.
Tradeoff: poll_database_for_updates() does repeated query + count aggregation every second; scaling can degrade.
Subprocess-based crypto integration
Why chosen: production vault crypto is implemented in C++ OpenSSL (strong primitives) and called from Python via subprocess.run([ENCRYPTOR_BIN, ...]).
Tradeoff: overhead and security considerations:
passwords/secrets appear on process argv (depending on OS visibility),
requires careful return-code/stdout handling (there are correctness issues in share_download, see weaknesses).
Custom “JWT-like” token vs standard JWT libraries
Why chosen: lightweight custom implementation inside the same C++ binary that already handles crypto.
Tradeoff: not fully compatible with JWT semantics and missing checks (e.g., no real one-time claim enforcement beyond DB flag).
Proxy enforcement model (thread pool + blocking I/O)
Why chosen: straightforward concurrency for TCP proxying and CONNECT relays.
Tradeoff: request/response buffering is not fully robust (limited recv() sizes and simplistic header parsing).
8. Strengths of the Project
End-to-end system integration with real-time observability
proxy/main.c → SQLite traffic_logs → api/websockets_manager.py → frontend/src/components/Dashboard.jsx is a complete, working pipeline for live analytics.
Crypto primitives are implemented correctly in principle (AES-GCM + PBKDF2 + HMAC)
api/encryptor/main.cpp uses AES-256-GCM streaming + PBKDF2-HMAC-SHA256 with a large iteration count and uses HMAC-SHA256 for token integrity.
Clean separation between enforcement and analytics/vault services
Proxy is “dumb” and logs/blocks; API/Frontend handle UX and higher-level workflows.
Practical UX patterns
frontend/src/components/Dashboard.jsx does baseline HTTP fetch + websocket live updates.
frontend/src/components/Threats.jsx periodically refreshes status and stats while allowing manual checks/additions.
9. Weaknesses / Possible Improvements
Critical: DB schema mismatches across schema.sql, SQLAlchemy models, and C/Python SQL usage
schema.sql defines threats with columns threat_category, date_added, and no is_active (it has active as a separate table structure for vault, and threats structure conflicts).
api/models.py defines Threat with category, added_timestamp, is_active.
proxy/main.c:is_domain_blocked() queries is_active.
api/threat_updater.py inserts into category, added_timestamp, is_active.
api/routers/threats.py queries category, added_timestamp.
Result: threat sync/blocking and threat endpoints are likely broken unless the DB is pre-existing with the “ORM-style” schema.
Improvement: make schema.sql match api/models.py column names exactly (or drop schema.sql on fresh DB and let SQLAlchemy create it).
Token verification / decrypt correctness issues
api/routers/vault.py:share_download() validates the token but does not validate decrypt success beyond subprocess return code.
In api/encryptor/main.cpp, the decrypt branch always returns 0 at the end of main() even on auth failure (it prints JSON error but the process exit code may still be success).
Improvement: make encryptor exit with non-zero status on decrypt/auth failure and/or parse stdout JSON in share_download() like decrypt_manual_file() does.
Vault temp decrypted-file lifecycle
share_download() writes decrypted output to VAULT_DIR (e.g., /vault/dec_<file_id>_<original_filename>) but never deletes it after serving.
Improvement: stream decrypted content without writing to disk or implement secure cleanup (and consider FileResponse background deletion).
Environment/config mismatches for secrets
Root .env defines NETSHIELD_SECRET, but api/routers/vault.py expects SERVER_SECRET.
Root .env does not define SERVER_MASTER_PASSWORD, but vault.py expects SERVER_MASTER_PASSWORD.
Improvement: unify env var names or update vault.py to use NETSHIELD_SECRET and consistent master password config.
Insecure/unrestricted API surface
No authentication/authorization on threats and vault endpoints (/api/threats/add, vault decrypt/upload/share).
api/main.py sets permissive CORS (allow_origins=["*"] with allow_credentials=True).
Improvement: add auth, rate limiting, and correct CORS configuration.
Proxy security risks due to string-built SQL and shell execution
proxy/main.c:log_to_db() uses string formatting to build INSERT statements (SQL injection risk if untrusted strings contain quotes).
The interception path builds a curl command string using intercept_filename extracted from remote headers and runs it via sh -c (execlp("sh","sh","-c", curl_cmd, ...)).
Improvement: parameterize SQL, avoid sh -c, and sanitize/quote filenames properly.
Analytics semantics mismatch
The C proxy sets traffic_logs.blocked to should_intercept for intercepted responses (so “blocked” can represent “vault-intercepted” rather than “threat-blocked”).
Improvement: split fields (e.g., threat_blocked vs vault_intercepted) for correct dashboard metrics.
Proxy parsing limitations
proxy_parse.c + proxy/main.c effectively support GET-only for non-CONNECT requests (parser rejects non-GET).
Improvement: implement broader HTTP parsing and robust streaming/body handling.
10. Resume-Ready Bullet Points
Built a full-stack network security gateway with a C-based TCP/HTTP proxy that enforces SQLite threat intelligence (proxy/main.c:is_domain_blocked) and logs traffic analytics (traffic_logs) for real-time monitoring.
Implemented real-time observability using FastAPI WebSockets with background SQLite polling (api/websockets_manager.py:poll_database_for_updates) and a React dashboard consuming live streams + chart aggregation (/api/traffic/chart).
Developed a secure encrypted vault service by integrating a C++ OpenSSL engine into a Python API (api/routers/vault.py calling api/encryptor/main.cpp) using AES-256-GCM + PBKDF2 key derivation and HMAC-signed share tokens.
Engineered threat-intel ingestion by fetching external blocklists and persisting them to SQLite (api/threat_updater.py:update_threats), enabling UI-driven refresh/check workflows (api/routers/threats.py) and automated proxy blocking.
If you want, I can also produce a “module interaction diagram” (text/mermaid-style) and a second pass that enumerates every endpoint and every DB table/column used by each component, specifically to pinpoint where the schema mismatches will break runtime behavior.