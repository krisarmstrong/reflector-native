Name:           reflector
Version:        2.0.0
Release:        1%{?dist}
Summary:        High-performance network packet reflector

License:        MIT
URL:            https://github.com/krisarmstrong/reflector-native
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  libcap-devel
%if 0%{?fedora} || 0%{?rhel} >= 8
BuildRequires:  libxdp-devel
BuildRequires:  libbpf-devel
%endif

Requires:       libcap
%if 0%{?fedora} || 0%{?rhel} >= 8
Recommends:     libxdp
Recommends:     libbpf
%endif

%description
Reflector is a high-performance network packet reflector designed for
network testing and ITO (In-band Test and Operations) packet reflection.

Features:
- AF_XDP zero-copy packet processing (10-40 Gbps)
- AF_PACKET fallback for universal compatibility
- Optional DPDK support for 100G+ line-rate
- RFC 2544, Y.1564, Y.1731, MEF, TSN packet reflection
- Multiple reflection modes (MAC, MAC+IP, ALL)
- NetAlly/Fluke compatible OUI filtering
- Systemd service with non-root operation via capabilities

%prep
%autosetup

%build
make linux %{?_smp_mflags}

%install
# Install binary
install -D -m 755 reflector-linux %{buildroot}%{_bindir}/reflector

# Install systemd service
install -D -m 644 scripts/service/reflector.service %{buildroot}%{_unitdir}/reflector.service

# Install config
install -D -m 644 reflector.yaml.example %{buildroot}%{_sysconfdir}/reflector/reflector.yaml

# Install environment file
install -D -m 644 packaging/debian/environment %{buildroot}%{_sysconfdir}/reflector/environment

# Create directories for logs and state
install -d -m 755 %{buildroot}%{_localstatedir}/log/reflector
install -d -m 755 %{buildroot}%{_localstatedir}/lib/reflector

%pre
# Create group if it doesn't exist
getent group reflector >/dev/null || groupadd -r reflector

# Create user if it doesn't exist
getent passwd reflector >/dev/null || \
    useradd -r -g reflector -d /var/lib/reflector -s /sbin/nologin \
    -c "Reflector packet reflector daemon" reflector

exit 0

%post
# Set file capabilities for non-root operation
setcap 'cap_net_raw,cap_net_admin,cap_sys_admin,cap_ipc_lock+ep' %{_bindir}/reflector || true

# Set ownership
chown reflector:reflector %{_localstatedir}/log/reflector
chown reflector:reflector %{_localstatedir}/lib/reflector
chown root:reflector %{_sysconfdir}/reflector
chmod 750 %{_sysconfdir}/reflector
chown root:reflector %{_sysconfdir}/reflector/reflector.yaml
chmod 640 %{_sysconfdir}/reflector/reflector.yaml

%systemd_post reflector.service

echo ""
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║  Reflector installed successfully!                        ║"
echo "╠═══════════════════════════════════════════════════════════╣"
echo "║  Edit config:    /etc/reflector/reflector.yaml            ║"
echo "║  Start service:  sudo systemctl start reflector           ║"
echo "║  Enable at boot: sudo systemctl enable reflector          ║"
echo "║  View logs:      journalctl -u reflector -f               ║"
echo "║                                                           ║"
echo "║  Manual usage:   reflector <interface> [options]          ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

%preun
%systemd_preun reflector.service

%postun
%systemd_postun_with_restart reflector.service

# Remove user/group on complete removal (not upgrade)
if [ $1 -eq 0 ]; then
    userdel reflector 2>/dev/null || true
    groupdel reflector 2>/dev/null || true
fi

%files
%license LICENSE
%doc README.md docs/
%{_bindir}/reflector
%{_unitdir}/reflector.service
%dir %attr(750,root,reflector) %{_sysconfdir}/reflector
%config(noreplace) %attr(640,root,reflector) %{_sysconfdir}/reflector/reflector.yaml
%config(noreplace) %{_sysconfdir}/reflector/environment
%dir %attr(755,reflector,reflector) %{_localstatedir}/log/reflector
%dir %attr(755,reflector,reflector) %{_localstatedir}/lib/reflector

%changelog
* Fri Dec 27 2024 Kris Armstrong <kris.armstrong@me.com> - 2.0.0-1
- Initial 2.0.0 release
- AF_PACKET and AF_XDP support
- TPACKET_V3 with automatic V2 fallback for veth interfaces
- RFC 2544, Y.1564, Y.1731, MEF, TSN packet reflection
- Systemd service with capability-based non-root operation
- Multiple reflection modes (MAC, MAC+IP, ALL)
