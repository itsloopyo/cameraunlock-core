FROM debian@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132

RUN rm /etc/apt/sources.list.d/debian.sources \
 && printf '%s\n' \
    'Types: deb' 'URIs: http://snapshot.debian.org/archive/debian/20260911T000000Z' 'Suites: trixie trixie-updates' \
    'Components: main' 'Signed-By: /usr/share/keyrings/debian-archive-keyring.pgp' 'Check-Valid-Until: no' '' \
    'Types: deb' 'URIs: http://snapshot.debian.org/archive/debian-security/20260911T000000Z' 'Suites: trixie-security' \
    'Components: main' 'Signed-By: /usr/share/keyrings/debian-archive-keyring.pgp' 'Check-Valid-Until: no' \
    > /etc/apt/sources.list.d/snapshot.sources \
 && apt-get update \
 && apt-get install -y --no-install-recommends wine wine64 mono-runtime libmono-system-core4.0-cil \
 && rm -rf /var/lib/apt/lists/*

# The wine-mono version and SHA-256 are the ones Debian's Wine 10.0 names in its appwiz.cpl, which
# installs it from /usr/share/wine/mono without a download.
ADD --checksum=sha256:cf6173ae94b79e9de13d9a74cdb2560a886fc3d271f9489acb1cfdbd961cacb2 \
    https://dl.winehq.org/wine/wine-mono/9.4.0/wine-mono-9.4.0-x86.msi /usr/share/wine/mono/wine-mono-9.4.0-x86.msi
RUN chmod 644 /usr/share/wine/mono/wine-mono-9.4.0-x86.msi \
 && useradd --uid 1000 --create-home --home-dir /opt/probe-build-home probe \
 && mkdir /opt/prefix && chown probe:probe /opt/prefix

# The template prefix each case copies into its tmpfs. mshtml= skips wine-gecko, which Wine would
# otherwise try to download.
USER probe
ENV LANG=C.UTF-8 WINEDEBUG=-all WINEARCH=win64
RUN export HOME=/opt/probe-build-home WINEPREFIX=/opt/prefix \
 && WINEDLLOVERRIDES="mshtml=" /usr/lib/wine/wine64 wineboot --init \
 && /usr/lib/wine/wineserver64 --wait \
 && test -d /opt/prefix/drive_c/windows/mono && test -f /opt/prefix/system.reg
