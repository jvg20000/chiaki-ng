# ────────────────────────────────────────────────────────────
# chiaki-ng build wrapper — todo corre dentro del contenedor.
# El host solo necesita Docker. Cero conflictos de versiones.
#
# Uso:
#   make image        Construir la imagen del contenedor
#   make configure    cmake configure dentro del contenedor
#   make build        Compilar chiaki (gui + cli)
#   make build-gui    Solo GUI
#   make test         Correr tests
#   make shell        Shell interactiva en el contenedor
#   make clean        Limpiar build/
# ────────────────────────────────────────────────────────────

IMAGE    := chiaki-devcontainer
WORKDIR  := $(shell pwd)
BUILD    := $(WORKDIR)/build
NPROC    := $(shell nproc 2>/dev/null || echo 4)

CMAKE_FLAGS := -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCHIAKI_ENABLE_TESTS=OFF \
	-DCHIAKI_ENABLE_CLI=ON \
	-DCHIAKI_ENABLE_GUI=ON

DOCKER_RUN := docker run --rm -u root \
	-v $(WORKDIR):/workspace \
	-w /workspace \
	$(IMAGE)

.PHONY: image configure build build-gui test shell clean

# ── Imagen ─────────────────────────────────────────────────
image:
	docker build -t $(IMAGE) -f .devcontainer/Dockerfile .devcontainer/

# ── Configure ───────────────────────────────────────────────
configure:
	@mkdir -p $(BUILD)
	$(DOCKER_RUN) bash -c "cd build && cmake .. $(CMAKE_FLAGS)"

# ── Build ───────────────────────────────────────────────────
build:
	$(DOCKER_RUN) bash -c "cd build && ninja -j$(NPROC)"

build-gui:
	$(DOCKER_RUN) bash -c "cd build && ninja -j$(NPROC) gui/chiaki"

# ── Test ────────────────────────────────────────────────────
test:
	$(DOCKER_RUN) bash -c "cd build && ctest --output-on-failure"

# ── Shell ───────────────────────────────────────────────────
shell:
	$(DOCKER_RUN) bash

# ── Clean ───────────────────────────────────────────────────
clean:
	rm -rf $(BUILD)
