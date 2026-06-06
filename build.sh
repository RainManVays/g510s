#!/bin/bash
# Build script for g510s — Logitech G510/G510s keyboard utility
# Linux Mint 22 / Ubuntu 24.04

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TMP="/tmp/g510s-build-deps"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[+]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
error() { echo -e "${RED}[✗]${NC} $*"; exit 1; }
step()  { echo -e "\n${GREEN}══ $* ══${NC}"; }


# ─── 1. System packages ───────────────────────────────────────────────────────

step "Устанавливаем системные зависимости"

sudo apt-get update -qq

sudo apt-get install -y \
    build-essential \
    git \
    autoconf \
    automake \
    libtool \
    pkg-config \
    libusb-1.0-0-dev \
    libudev-dev \
    libg15render-dev \
    libgtk-3-dev \
    libayatana-appindicator3-dev

info "Системные пакеты установлены"


# ─── 2. Compatibility shim: appindicator3-0.1 → ayatana ──────────────────────

step "Настраиваем совместимость appindicator3"

# On modern Ubuntu/Mint the classic libappindicator3 is replaced by the
# Ayatana fork: different pkg-config name and different header directory.
# We solve both without touching /usr:
#   - pkg-config: create a compat .pc file in a local dir, add to PKG_CONFIG_PATH
#   - headers:    create a local compat dir with a symlink, add to CFLAGS via -I

COMPAT_DIR="${SCRIPT_DIR}/.compat"
mkdir -p "${COMPAT_DIR}/pkgconfig" "${COMPAT_DIR}/include/libappindicator"

# pkg-config compat
if ! pkg-config --exists appindicator3-0.1 2>/dev/null; then
    if pkg-config --exists ayatana-appindicator3-0.1 2>/dev/null; then
        info "Создаём compat pkg-config: appindicator3-0.1 → ayatana"
        cat > "${COMPAT_DIR}/pkgconfig/appindicator3-0.1.pc" <<'EOF'
Name: AppIndicator3 (compat)
Description: Compatibility wrapper for ayatana-appindicator3-0.1
Version: 0.1
Requires: ayatana-appindicator3-0.1
EOF
        # Makefile picks up COMPAT_DIR automatically (default: .compat)
    else
        error "Не найден ни appindicator3-0.1 ни ayatana-appindicator3-0.1"
    fi
else
    info "pkg-config appindicator3-0.1 уже доступен"
fi

# Header compat: <libappindicator/app-indicator.h> → libayatana-appindicator
# The header may be in a versioned subdir, so we search dynamically.
if [ -f "/usr/include/libappindicator/app-indicator.h" ]; then
    info "Заголовки libappindicator уже доступны системно"
else
    AYATANA_HDR="$(find /usr/include -path '*/libayatana-appindicator/app-indicator.h' 2>/dev/null | head -1)"
    if [ -n "${AYATANA_HDR}" ]; then
        info "Создаём локальный compat-каталог заголовков appindicator"
        ln -sfn "${AYATANA_HDR}" "${COMPAT_DIR}/include/libappindicator/app-indicator.h"
        # Makefile adds -I$(COMPAT_DIR)/include automatically
        info "Compat заголовок: ${AYATANA_HDR}"
    else
        error "Заголовок app-indicator.h не найден — установите libayatana-appindicator3-dev"
    fi
fi


# ─── 3a. Build libg15render from source (fixes heap-use-after-free in apt pkg) ─

step "Собираем libg15render из исходников (исправление UAF в g15r_loadG15Font)"

# The apt package libg15render 1.3.0~svn316-3 has a heap-use-after-free in
# g15r_loadG15Font: it calls realloc() then fread()s into the freed old pointer.
# vividnightmare/libg15render v1.3.1 is bundled in libg15render/ — no external
# clone needed. configure.ac already contains the AC_PREREQ([2.71]) patch.
LIBG15RENDER_DIR="${BUILD_TMP}/libg15render"

mkdir -p "${LIBG15RENDER_DIR}"
info "Копируем исходники libg15render из репозитория"
cp -r "${SCRIPT_DIR}/libg15render/." "${LIBG15RENDER_DIR}/"

cd "${LIBG15RENDER_DIR}"
info "Запускаем autoreconf"
autoreconf -fi

info "Конфигурируем libg15render"
./configure --prefix=/usr/local

info "Собираем libg15render"
make -j"$(nproc)"

info "Устанавливаем libg15render"
sudo make install
sudo ldconfig

info "libg15render установлен из исходников"


# ─── 3. Build patched libg15 (bundled in repo) ────────────────────────────────

step "Собираем patched libg15 (из репозитория)"

# Patched libg15 is bundled in libg15/ — no external clone needed.
# Patches vs vividnightmare/libg15:
#   - ported libusb-0.1 → libusb-1.0 (fixes FD_SETSIZE crash)
#   - fixed buffer overflow in slow LCD write path
#   - fixed exitLibG15: release correct interface, re-attach kernel driver

mkdir -p "${BUILD_TMP}"
LIBG15_DIR="${BUILD_TMP}/libg15"

info "Копируем исходники libg15 из репозитория"
mkdir -p "${LIBG15_DIR}"
cp -r "${SCRIPT_DIR}/libg15/." "${LIBG15_DIR}/"

cd "${LIBG15_DIR}"
info "Запускаем autoreconf"
autoreconf -fi

info "Конфигурируем libg15"
./configure --prefix=/usr/local

info "Собираем libg15"
make -j"$(nproc)"

info "Устанавливаем libg15"
sudo make install

# Make the new library discoverable
sudo ldconfig

info "libg15 установлен в /usr/local"

# Verify
if pkg-config --exists libg15; then
    info "pkg-config нашёл libg15: $(pkg-config --modversion libg15)"
else
    # libg15 may not have a .pc file — check by looking for the header
    if [ -f /usr/local/include/libg15.h ]; then
        info "libg15.h найден в /usr/local/include"
        # Export paths for the build below
        # Makefile adds -I/usr/local/include and -L/usr/local/lib automatically
    else
        error "libg15 не установился — заголовок не найден"
    fi
fi


# ─── 4. Build g510s ───────────────────────────────────────────────────────────

step "Собираем g510s"

cd "${SCRIPT_DIR}"
make clean 2>/dev/null || true

# If libg15 installed to /usr/local, pass the path explicitly.
# pkg-config picks it up if /usr/local/lib/pkgconfig is in PKG_CONFIG_PATH.
# PKG_CONFIG_PATH is set by the Makefile

make -j"$(nproc)"

info "g510s собран успешно"


# ─── 5. Install g510s ─────────────────────────────────────────────────────────

step "Устанавливаем g510s"

# stop running instance so the binary can be replaced
if pgrep -x g510s > /dev/null 2>&1; then
    info "Останавливаем запущенный g510s..."
    pkill -x g510s || true
    sleep 1
fi

sudo make install

info "Файлы установлены:"
info "  /usr/local/bin/g510s"
info "  /usr/local/share/g510s/"
info "  /lib/udev/rules.d/99-g510s.rules"
info "  /etc/xdg/autostart/g510s.desktop"


# ─── 6. User group and udev ───────────────────────────────────────────────────

step "Настраиваем udev и права доступа"

CURRENT_USER="${SUDO_USER:-${USER}}"

# Add user to plugdev group (required by udev rule for USB access)
if ! groups "${CURRENT_USER}" | grep -q plugdev; then
    info "Добавляем пользователя ${CURRENT_USER} в группу plugdev"
    sudo usermod -aG plugdev "${CURRENT_USER}"
    warn "Группа plugdev добавлена — нужен ВЫХОД и ВХОД в систему (или reboot)"
else
    info "Пользователь ${CURRENT_USER} уже в группе plugdev"
fi

# Reload udev rules and apply to already-connected devices
info "Перезагружаем правила udev"
sudo udevadm control --reload-rules
sudo udevadm trigger --action=add --subsystem-match=usb
sudo udevadm trigger --action=add --subsystem-match=input

info "udev перезагружен"


# ─── Done ─────────────────────────────────────────────────────────────────────

echo ""
echo -e "${GREEN}════════════════════════════════════════${NC}"
echo -e "${GREEN}  g510s успешно собран и установлен!    ${NC}"
echo -e "${GREEN}════════════════════════════════════════${NC}"
echo ""
echo "Запуск:"
echo "  g510s"
echo ""
echo "Опции:"
echo "  g510s --help"
echo ""

if ! groups "${CURRENT_USER}" | grep -q plugdev; then
    echo -e "${YELLOW}ВНИМАНИЕ: Выйдите из системы и войдите снова (или перезагрузитесь)${NC}"
    echo -e "${YELLOW}чтобы применились права группы plugdev для доступа к клавиатуре.${NC}"
fi
