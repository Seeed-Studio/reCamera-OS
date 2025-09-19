#!/bin/bash

# Define build target and Docker image name
# (provide default if not passed)
TARGET=${1:-"sg2002_recamera_emmc"}
IMAGE_NAME="recamera-os-builder"
DOCKERFILE=".devcontainer/Dockerfile"
# Determine absolute project root (script location) so script can be run from anywhere
PROJECT_ROOT=$(cd "$(dirname "$0")" && pwd)

# Check Dockerfile exists
if [ ! -f "$DOCKERFILE" ]; then
    echo "Error: Dockerfile '$DOCKERFILE' not found."
    exit 1
fi

# Build image if missing
if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
    echo "Build image '$IMAGE_NAME' not found, building..."
    docker build -t "$IMAGE_NAME" -f "$DOCKERFILE" .
    if [ $? -ne 0 ]; then
        echo "Error: Docker image build failed."
        exit 1
    fi
else
    echo "Build image '$IMAGE_NAME' already exists."
fi

echo "============================================="
echo "Starting build inside Docker container (non-root user)..."
echo "Project directory: $PROJECT_ROOT"
echo "Build target: $TARGET"
echo "============================================="

# Run Docker container and execute build
# --rm: auto remove container
# -it: interactive logs
HOST_UID=$(id -u)
HOST_GID=$(id -g)
HOST_UNAME=${USER:-hostuser}
DOCKER_HOME_DIR="$PROJECT_ROOT/output/.docker_home"
mkdir -p "$DOCKER_HOME_DIR"

docker run --rm -it \
    -e HOST_UID=$HOST_UID -e HOST_GID=$HOST_GID -e HOST_UNAME=$HOST_UNAME \
    -e HOME=/home/$HOST_UNAME \
    -v "$PROJECT_ROOT":/work \
    -v "$DOCKER_HOME_DIR":/home/$HOST_UNAME \
    --workdir /work \
    "$IMAGE_NAME" \
    bash -c "
set -e
# Create matching user (idempotent)
if ! id \"$HOST_UNAME\" >/dev/null 2>&1; then
        groupadd -g $HOST_GID $HOST_UNAME 2>/dev/null || true
        useradd -m -u $HOST_UID -g $HOST_GID -s /bin/bash $HOST_UNAME 2>/dev/null || true
fi
chown -R $HOST_UID:$HOST_GID /home/$HOST_UNAME || true
mkdir -p /work/output || true
chown -R $HOST_UID:$HOST_GID /work/output || true
echo '>>> Using user:'
id "$HOST_UNAME"

cat > /tmp/inner_build.sh <<'EOF_INNER'
#!/bin/bash
set -e
echo '>>> Preflight: Python availability...'
if ! command -v python >/dev/null 2>&1; then
    echo 'Error: python command not found (python3 required)'; exit 2; fi
if ! command -v pkg-config >/dev/null 2>&1; then
    echo 'Error: missing pkg-config (rebuild image)'; exit 2; fi
python - <<'PYEOF'
import importlib,sys
missing=[]
for m in ['jinja2','yaml']:
        try: importlib.import_module(m)
        except Exception: missing.append(m)
if missing:
        print('Error: missing Python deps:', ','.join(missing))
        sys.exit(3)
PYEOF
echo '>>> 0. Configure Git safe.directory...'
git config --global --add safe.directory /work
find output -maxdepth 4 -type d -name .git 2>/dev/null | while read g; do
    repo_dir=$(dirname "$g"); git config --global --add safe.directory /work/$repo_dir || true; done
echo '>>> 1. Start building target: ${TARGET}...'
make ${TARGET}
echo '>>> Build finished. Artifacts at output/${TARGET}.'
EOF_INNER

chmod +x /tmp/inner_build.sh
# Prefer sudo if present else su
if command -v sudo >/dev/null 2>&1; then
    sudo -u $HOST_UNAME /tmp/inner_build.sh
else
    su -s /bin/bash -c /tmp/inner_build.sh $HOST_UNAME
fi
"

# 检查上一个命令的退出状态
if [ $? -eq 0 ]; then
    echo "============================================="
    echo "Build succeeded!"
    echo "============================================="
else
    echo "============================================="
    echo "Build failed."
    echo "============================================="
    exit 1
fi
