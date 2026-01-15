#!/bin/bash
# Local build script that replicates the GitHub Actions workflow
# Usage:
#   ./build-local.sh              # Build for local platform only, no push
#   ./build-local.sh --push       # Build multi-platform and push to ghcr.io
#   ./build-local.sh --tag mytag  # Use custom tag instead of branch name
#   ./build-local.sh --load       # Build and load into local Docker (single platform only)

set -euo pipefail

cd "$(dirname "$0")"

# Defaults
PUSH=false
LOAD=false
TAG="local"
PLATFORMS="linux/amd64,linux/arm/v7,linux/arm64/v8"
DOCKER_IMAGE="ghcr.io/brewblox/brewblox-usb-proxy"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --push)
            PUSH=true
            shift
            ;;
        --load)
            LOAD=true
            shift
            ;;
        --tag)
            TAG="$2"
            shift 2
            ;;
        --platform)
            PLATFORMS="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --push              Build multi-platform and push to ghcr.io"
            echo "  --load              Build and load into local Docker (single platform only)"
            echo "  --tag TAG           Use custom tag (default: local)"
            echo "  --platform PLAT     Comma-separated platforms (default: linux/amd64,linux/arm/v7,linux/arm64/v8)"
            echo "  --help, -h          Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                  # Build for all platforms (no push, no load)"
            echo "  $0 --load           # Build for local platform and load into Docker"
            echo "  $0 --push           # Build multi-platform and push to registry"
            echo "  $0 --tag dev --load # Build with custom tag and load locally"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo "========================================"
echo "BrewBlox USB Proxy - Local Build Script"
echo "========================================"
echo "Image: $DOCKER_IMAGE:$TAG"
echo "Push: $PUSH"
echo "Load: $LOAD"
echo "Platforms: $PLATFORMS"
echo "========================================"

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed or not in PATH"
    exit 1
fi

# Build the Docker image (equivalent to docker/build-push-action)
build_image() {
    echo ""
    echo "Building Docker image..."

    BUILD_ARGS=(
        "--file" "Dockerfile"
        "--tag" "$DOCKER_IMAGE:$TAG"
    )

    # Add platform(s)
    if [ "$LOAD" = true ]; then
        # --load only works with single platform, use host platform
        echo "Note: --load specified, building for local platform only"
        BUILD_ARGS+=("--load")
    else
        BUILD_ARGS+=("--platform" "$PLATFORMS")
    fi

    # Add push flag if requested
    if [ "$PUSH" = true ]; then
        BUILD_ARGS+=("--push")
    fi

    # Add labels similar to docker/metadata-action
    BUILD_ARGS+=(
        "--label" "org.opencontainers.image.source=https://github.com/BrewBlox/brewblox-usb-proxy"
        "--label" "org.opencontainers.image.revision=$(git rev-parse HEAD 2>/dev/null || echo 'unknown')"
        "--label" "org.opencontainers.image.created=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    )

    # Context is current directory
    BUILD_ARGS+=(".")

    echo "Running: docker buildx build ${BUILD_ARGS[*]}"
    echo ""

    docker buildx build "${BUILD_ARGS[@]}"
}

# Main execution
main() {
    # Build the image
    build_image

    echo ""
    echo "========================================"
    echo "Build complete!"
    echo "Image: $DOCKER_IMAGE:$TAG"
    if [ "$LOAD" = true ]; then
        echo ""
        echo "Image loaded into local Docker. Run with:"
        echo "  docker run --rm -p 5000:5000 $DOCKER_IMAGE:$TAG"
    fi
    if [ "$PUSH" = true ]; then
        echo "Image pushed to ghcr.io"
    fi
    echo "========================================"
}

main
