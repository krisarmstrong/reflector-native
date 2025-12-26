Name:           reflector
Version:        2.0.0
Release:        1%{?dist}
Summary:        High-performance network packet reflector

License:        MIT
URL:            https://github.com/krisarmstrong/reflector-native
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  golang >= 1.21
BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  libxdp-devel
BuildRequires:  libbpf-devel
BuildRequires:  elfutils-libelf-devel
BuildRequires:  zlib-devel
BuildRequires:  nodejs
BuildRequires:  npm

Requires:       libxdp
Requires:       libbpf

%description
Reflector is a high-performance network packet reflector designed for
network testing and ITO (In-band Test and Operations) packet reflection.

Features:
- AF_XDP zero-copy packet processing (10-40 Gbps)
- Optional DPDK support for 100G+ line-rate
- Terminal UI (TUI) dashboard
- Embedded web UI
- ITO packet filtering (NetAlly/Fluke compatible)
- Multiple reflection modes (MAC, MAC+IP, ALL)

%prep
%setup -q

%build
make v2

%install
install -D -m 755 reflector %{buildroot}%{_bindir}/reflector
install -D -m 644 reflector.yaml.example %{buildroot}%{_sysconfdir}/reflector/reflector.yaml
install -D -m 644 scripts/service/reflector.service %{buildroot}%{_unitdir}/reflector.service

%post
%systemd_post reflector.service

%preun
%systemd_preun reflector.service

%postun
%systemd_postun_with_restart reflector.service

%files
%license LICENSE
%doc README.md docs/ARCHITECTURE.md
%{_bindir}/reflector
%config(noreplace) %{_sysconfdir}/reflector/reflector.yaml
%{_unitdir}/reflector.service

%changelog
* Wed Dec 25 2024 Kris Armstrong <kris.armstrong@me.com> - 2.0.0-1
- Initial 2.0.0 release with Go control plane
- Added TUI dashboard
- Added embedded web UI
- Added YAML configuration support
- Added systemd service integration
