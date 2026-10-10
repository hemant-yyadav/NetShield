# NetShield

NetShield is a containerized network-security dashboard and proxy. It filters HTTP/HTTPS traffic against a shared threat list, records traffic activity, provides a live React dashboard, and includes an encrypted file vault with expiring one-time share links.

<!-- > This is a lab/prototype project. Review and harden its defaults before deploying it to a real network or storing sensitive data. -->

## What is included

- **C proxy** (`proxy/`) — listens for HTTP and HTTPS `CONNECT` traffic, checks domains against the SQLite threat table, and logs decisions.
- **FastAPI service** (`api/`) — exposes traffic analytics, threat-management endpoints, WebSocket updates, and vault operations.
- **React + Vite dashboard** (`frontend/`) — displays traffic, threat intelligence, and vault workflows.
- **SQLite database** (`schema.sql`) — shared persistence for proxy decisions and API/dashboard data.
- **C++/OpenSSL encryptor** (`api/encryptor/`) — encrypts vault files and signs/verifies share tokens.

## Quick start (Docker)

### Prerequisites

- Docker Desktop with Docker Compose
- A free host port for each of `3000` and `8888`

Create the deployment configuration from the example, then replace the placeholder secrets.

```env
FRONTEND_PORT=3000
PROXY_PORT=8888
DB_PATH=/data/netshield.db
VAULT_PATH=/vault
SERVER_SECRET=replace-with-a-random-secret-of-at-least-32-characters
SERVER_MASTER_PASSWORD=replace-with-a-strong-vault-password-of-at-least-16-characters
VAULT_EXPIRY_HOURS=1
```

Build and start all services:

```bash
docker compose up --build -d
```

The API is private to the Docker network. The production frontend forwards `/api` and WebSocket traffic to it.

Then open:

| Service | Address |
| --- | --- |
| Dashboard | http://localhost:3000 |
| API / OpenAPI docs | http://localhost:3000/docs |
| Forward proxy | `localhost:8888` |

View service logs with `docker compose logs -f`, and stop the stack with `docker compose down`.

The repository Makefile offers equivalent shortcuts: `make build`, `make up`, `make logs`, and `make down`. `make reset` removes Docker volumes, including the database and vault data.

## Use the proxy

Configure a browser or HTTP client to use `localhost:8888` as its HTTP and HTTPS proxy. The proxy allows domains not present in the `threats` table and blocks active listed domains. Each decision is written to the shared SQLite database and becomes visible through the dashboard/API.

## Main API routes

| Area | Selected routes |
| --- | --- |
| Health | `GET /` |
| Traffic | `GET /api/traffic/logs`, `/stats`, `/chart`; WebSocket `/api/traffic/live` |
| Threats | `POST /api/threats/refresh`, `POST /api/threats/add`, `GET /api/threats/check?domain=example.com`, `/stats`, `/recent`, `/status` |
| Vault | `POST /api/vault/upload`, `/share/generate`, `/decrypt`; `GET /api/vault/files`, `/share/download/{token}` |

Interactive request schemas and responses are available at `/docs` while the API is running.

## Threat intelligence

Calling `POST /api/threats/refresh` runs the background updater, which downloads host-based indicators from StevenBlack and URLHaus and inserts them into the local threat table. Internet access is required for that operation.

## Local development

The containers are the supported integrated workflow, since the proxy and API share the SQLite database and the API expects the compiled vault encryptor. For focused UI work:

```bash
cd frontend
npm install
npm run dev
```

The Vite development server proxies `/api` calls to `http://127.0.0.1:8000`. The Docker image instead builds static assets and serves them with Nginx, which forwards API and WebSocket traffic to the internal API service.

## Repository map

```text
api/          FastAPI application, API routers, DB models, threat updater, native encryptor
frontend/     React/Vite operator dashboard
proxy/        C forward-proxy implementation and Docker build
schema.sql    Initial SQLite schema
docker-compose.yml  Full local stack definition
docs/         Detailed engineering manual
```

For a deeper architectural explanation and code walkthrough, see [the engineering manual](docs/NETSHIELD_ENGINEERING_MANUAL.md).

## Security notes

- Set unique, strong values for `SERVER_SECRET` and `SERVER_MASTER_PASSWORD`; the in-code fallback values are not safe.
- The API currently permits all CORS origins, so restrict that policy for real deployments.
- Put the dashboard and API behind authentication and TLS before exposing them beyond a trusted development environment.
- Treat Docker volume removal as data loss: it deletes the SQLite state and encrypted vault files.
