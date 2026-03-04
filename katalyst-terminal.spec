Name:           katalyst-terminal
Version:        0.1.0
Release:        1%{?dist}
Summary:        Vulkan-accelerated C++23 terminal emulator for Katalyst

License:        Apache-2.0
URL:            https://github.com/katalyst/katalyst-terminal
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.24
BuildRequires:  gcc-c++ >= 15.0
BuildRequires:  qt6-qtbase-devel
BuildRequires:  vulkan-headers
BuildRequires:  vulkan-loader-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  glslang

Requires:       qt6-qtbase
Requires:       vulkan-loader
Requires:       kf6-kconfig

%description
A fast, Vulkan-accelerated C++23 modular terminal emulator designed for Katalyst, utilizing Qt6 for UI integration and POSIX PTY capabilities.

%prep
%autosetup

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%files
%{_bindir}/katalyst-terminal
%{_datadir}/applications/org.katalyst.Terminal.desktop
%{_datadir}/katalyst-terminal/shaders/*
