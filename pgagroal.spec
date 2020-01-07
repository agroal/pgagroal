Name:          pgagroal
Version:       0.5.0
Release:       1%{dist}
Summary:       High-performance connection pool for PostgreSQL
License:       BSD
URL:           https://github.com/agroal/pgagroal
BuildRequires: gcc
BuildRequires: cmake
BuildRequires: make
BuildRequires: libev
BuildRequires: libev-devel
Requires:      libev
Source:        https://github.com/agroal/pgagroal/releases/%{name}-%{version}.tar.gz

%description
pgagroal is a high-performance connection pool for PostgreSQL.

%prep
%setup -q

%build

mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make

%install

mkdir -p %{buildroot}%{_sysconfdir}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_docdir}/%{name}/images
mkdir -p %{buildroot}%{_mandir}/man1
mkdir -p %{buildroot}%{_mandir}/man5
mkdir -p %{buildroot}%{_sysconfdir}/pgagroal

/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/README.md %{buildroot}%{_docdir}/%{name}/README.md
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/ARCHITECTURE.md %{buildroot}%{_docdir}/%{name}/ARCHITECTURE.md
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/CONFIGURATION.md %{buildroot}%{_docdir}/%{name}/CONFIGURATION.md
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/GETTING_STARTED.md %{buildroot}%{_docdir}/%{name}/GETTING_STARTED.md
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/PERFORMANCE.md %{buildroot}%{_docdir}/%{name}/PERFORMANCE.md
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/RPM.md %{buildroot}%{_docdir}/%{name}/RPM.md
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/images/perf-extended.png %{buildroot}%{_docdir}/%{name}/images/perf-extended.png
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/images/perf-prepared.png %{buildroot}%{_docdir}/%{name}/images/perf-prepared.png
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/images/perf-readonly.png %{buildroot}%{_docdir}/%{name}/images/perf-readonly.png
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/images/perf-simple.png %{buildroot}%{_docdir}/%{name}/images/perf-simple.png

/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgagroal.conf %{buildroot}%{_sysconfdir}/pgagroal/pgagroal.conf
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgagroal_hba.conf %{buildroot}%{_sysconfdir}/pgagroal/pgagroal_hba.conf

/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal.1 %{buildroot}%{_mandir}/man1/pgagroal.1
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal-admin.1 %{buildroot}%{_mandir}/man1/pgagroal-admin.1
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal-cli.1 %{buildroot}%{_mandir}/man1/pgagroal-cli.1
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal.conf.5 %{buildroot}%{_mandir}/man5/pgagroal.conf.5
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal_databases.conf.5 %{buildroot}%{_mandir}/man5/pgagroal_databases.conf.5
/usr/bin/install -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal_hba.conf.5 %{buildroot}%{_mandir}/man5/pgagroal_hba.conf.5

/usr/bin/install -m 755 %{_builddir}/%{name}-%{version}/build/src/pgagroal %{buildroot}%{_bindir}/pgagroal
/usr/bin/install -m 755 %{_builddir}/%{name}-%{version}/build/src/pgagroal-cli %{buildroot}%{_bindir}/pgagroal-cli
/usr/bin/install -m 755 %{_builddir}/%{name}-%{version}/build/src/pgagroal-admin %{buildroot}%{_bindir}/pgagroal-admin

/usr/bin/install -m 755 %{_builddir}/%{name}-%{version}/build/src/libpgagroal.so.%{version} %{buildroot}%{_libdir}/libpgagroal.so.%{version}

chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgagroal
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgagroal-cli
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgagroal-admin

cd %{buildroot}%{_libdir}/
ln -s -f libpgagroal.so.%{version} libpgagroal.so.0
ln -s -f libpgagroal.so.0 libpgagroal.so

%files
%license %{_docdir}/%{name}/LICENSE
%{_docdir}/%{name}/ARCHITECTURE.md
%{_docdir}/%{name}/CONFIGURATION.md
%{_docdir}/%{name}/GETTING_STARTED.md
%{_docdir}/%{name}/PERFORMANCE.md
%{_docdir}/%{name}/README.md
%{_docdir}/%{name}/RPM.md
%{_docdir}/%{name}/images/perf-extended.png
%{_docdir}/%{name}/images/perf-prepared.png
%{_docdir}/%{name}/images/perf-readonly.png
%{_docdir}/%{name}/images/perf-simple.png
%{_mandir}/man1/pgagroal.1
%{_mandir}/man1/pgagroal-admin.1
%{_mandir}/man1/pgagroal-cli.1
%{_mandir}/man5/pgagroal.conf.5
%{_mandir}/man5/pgagroal_databases.conf.5
%{_mandir}/man5/pgagroal_hba.conf.5
%config %{_sysconfdir}/pgagroal.conf
%config %{_sysconfdir}/pgagroal_hba.conf
%{_bindir}/pgagroal
%{_bindir}/pgagroal-cli
%{_bindir}/pgagroal-admin
%{_libdir}/libpgagroal.so
%{_libdir}/libpgagroal.so.0
%{_libdir}/libpgagroal.so.%{version}

%changelog
