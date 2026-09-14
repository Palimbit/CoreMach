# Fedora RPM spec file for BluMach
#
# To create RPM files from this spec file, run the following commands:
#  sudo dnf install rpm-build
#  mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
#
# copy this BluMach.spec file to ~/rpmbuild/SPECS and run the following commands:
#  cd ~/rpmbuild
#  sudo dnf builddep SPECS/BluMach.spec
#  rpmbuild --undefine=_disable_source_fetch -ba SPECS/BluMach.spec
#
# After a successful build, you can install the RPMs as follows:
#  sudo dnf install RPMS/$(uname -m)/blumach-*

Name:		blumach
Version:	7.0
Release:	1%{?dist}
Summary:	Classic PC emulator
License:	GPLv2+
URL:		https://github.com/BluMach/BluMach

Source0:	https://github.com/BluMach/BluMach/archive/refs/tags/v%{version}.tar.gz

BuildRequires: cmake
BuildRequires: desktop-file-utils
BuildRequires: extra-cmake-modules
BuildRequires: fluidsynth-devel
BuildRequires: freetype-devel
BuildRequires: gcc-c++
BuildRequires: libFAudio-devel
BuildRequires: libappstream-glib
BuildRequires: libatomic
BuildRequires: libevdev-devel
BuildRequires: libslirp-devel
BuildRequires: libxkbcommon-x11-devel
BuildRequires: libXi-devel
BuildRequires: ninja-build
BuildRequires: openal-soft-devel
BuildRequires: qt6-linguist
BuildRequires: qt6-qtbase-devel
BuildRequires: qt6-qtbase-private-devel
BuildRequires: rtmidi-devel
BuildRequires: wayland-devel
BuildRequires: SDL3-devel

Requires: hicolor-icon-theme
Requires: fluid-soundfont-gm
%description
BluMach is a preservation-focused emulator derived from 86Box. It combines
low-level x86 emulation with a curated historical catalogue, explicit evidence
and reproducible machine configurations. Firmware is not included.

%prep
%autosetup -p1 -n BluMach-%{version}

%build
%ifarch x86_64
  %cmake -DRELEASE=on
%else
  %ifarch arm aarch64
    %cmake -DRELEASE=on -DNEW_DYNAREC=on
  %else
    %cmake -DRELEASE=on -DDYNAREC=off
  %endif
%endif
%cmake_build

%install
# install base package
%cmake_install

# install icons
for i in 16 20 24 32 40 48 64 72 128 256; do
  mkdir -p $RPM_BUILD_ROOT%{_datadir}/icons/hicolor/${i}x${i}/apps
  cp src/unix/assets/${i}x${i}/io.github.BluMach.BluMach.png $RPM_BUILD_ROOT%{_datadir}/icons/hicolor/${i}x${i}/apps
done

# install desktop file
desktop-file-install --dir=%{buildroot}%{_datadir}/applications src/unix/assets/io.github.BluMach.BluMach.desktop

# install metadata
mkdir -p %{buildroot}%{_metainfodir}
cp src/unix/assets/io.github.BluMach.BluMach.metainfo.xml %{buildroot}%{_metainfodir}
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/io.github.BluMach.BluMach.metainfo.xml

# files part of the main package
%files
%license COPYING
%{_bindir}/BluMach
%{_datadir}/applications/io.github.BluMach.BluMach.desktop
%{_metainfodir}/io.github.BluMach.BluMach.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/io.github.BluMach.BluMach.png

%changelog
* Sat Aug 31 Jasmine Iwanek <jriwanek[AT]gmail.com> 7.0-1
- Bump release
