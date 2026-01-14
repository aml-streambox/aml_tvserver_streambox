SUMMARY = "StreamBox Config - Cockpit Plugin"
DESCRIPTION = "Cockpit web UI plugin for managing StreamBox TV configuration"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://backend/ \
           file://frontend/ \
           file://cockpit-streambox-config.service \
           file://cockpit-streambox-config.socket"

S = "${WORKDIR}"

RDEPENDS:${PN} = "python3 python3-flask cockpit"

inherit systemd

SYSTEMD_SERVICE:${PN} = "cockpit-streambox-config.socket"

do_install() {
    # Install backend
    install -d ${D}${datadir}/cockpit-streambox-config
    install -m 0755 ${WORKDIR}/backend/main.py ${D}${datadir}/cockpit-streambox-config/
    install -m 0644 ${WORKDIR}/backend/api.py ${D}${datadir}/cockpit-streambox-config/

    # Install frontend
    install -d ${D}${datadir}/cockpit/streambox-config
    install -m 0644 ${WORKDIR}/frontend/* ${D}${datadir}/cockpit/streambox-config/

    # Install systemd units
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/cockpit-streambox-config.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/cockpit-streambox-config.socket ${D}${systemd_system_unitdir}/
}

FILES:${PN} = "${datadir}/cockpit-streambox-config \
               ${datadir}/cockpit/streambox-config"
