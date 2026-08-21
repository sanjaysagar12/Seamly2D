# Pattern Agent: one image, built FROM the seamly2d-actiond base image (build it first from the
# repo root: `docker build -f Dockerfile.actiond -t seamly2d-actiond:latest .`). Serves both the
# API and the built React frontend from a single uvicorn process -- see app/main.py's static
# mount at the bottom of the file. Build context is pattern_agent/ (the parent of this file's own
# directory), not pattern_agent/backend/, so this stage can also reach ../frontend.
#
# Build: docker build -f pattern_agent/backend/Dockerfile -t pattern-agent:latest pattern_agent

# Must be declared before the first FROM (global scope) to be usable in stage 2's FROM below --
# an ARG declared between FROM instructions is scoped to the stage it trails, not global, and
# stage 2's FROM would fail with "base name should not be blank" (confirmed by trying it).
ARG BASE_IMAGE=seamly2d-actiond:latest

# ---------------------------------------------------------------------------------------------
# Stage 1: build the React frontend into static assets (frontend/dist/).
# ---------------------------------------------------------------------------------------------
FROM node:20-slim AS frontend-builder
WORKDIR /frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ .
RUN npm run build

# ---------------------------------------------------------------------------------------------
# Stage 2: the actual runtime -- seamly2d-actiond base + Python, serving API + static frontend.
# ---------------------------------------------------------------------------------------------
FROM ${BASE_IMAGE}

# The base image's own ENTRYPOINT/HEALTHCHECK target actiond directly; this image serves a
# FastAPI app instead, so both are replaced below. USER root only for package installs --
# execution still ends up as the base image's existing non-root "actiond" user (uid/gid 10001).
USER root
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 \
        python3-pip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/backend
COPY backend/requirements.txt .
# --break-system-packages: Debian's system Python is "externally managed" (PEP 668) by default;
# safe here since the whole container *is* the isolated environment already, no venv needed.
RUN pip3 install --no-cache-dir --break-system-packages -r requirements.txt

COPY backend/app ./app
COPY --from=frontend-builder /frontend/dist /app/frontend-dist

# ACTIOND_EXE overrides config.py's Windows-shaped default (out/.../actiond.exe) to the Linux
# binary this base image already ships. PATTERN_AGENT_DATA_DIR keeps SQLite + session files
# under a path this Dockerfile explicitly chowns below, since the base image's WORKDIR /app is
# owned by actiond:actiond but /app/backend is new and copied in as root. FRONTEND_DIST_DIR
# points main.py's static mount (see app/main.py) at the build output copied in above.
ENV ACTIOND_EXE=/app/bin/actiond \
    PATTERN_AGENT_DATA_DIR=/app/backend/data \
    FRONTEND_DIST_DIR=/app/frontend-dist \
    PYTHONUNBUFFERED=1

RUN mkdir -p /app/backend/data/measurements /app/backend/data/sessions \
    && chown -R actiond:actiond /app/backend

EXPOSE 6100

USER actiond

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD python3 -c "import urllib.request,sys; sys.exit(0 if urllib.request.urlopen('http://127.0.0.1:6100/api/measurements', timeout=3).status == 200 else 1)"

ENTRYPOINT ["python3", "-m", "uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "6100"]
