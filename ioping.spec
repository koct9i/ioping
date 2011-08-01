Name:		ioping
Version:	0.6
Release:	1%{?dist}
Summary:	simple disk I/O latency monitoring tool

Group:		Applications/System
License:	GPLv3+
URL:		http://code.google.com/p/ioping
Source0:	ioping-%{version}.tar.gz
BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

BuildRequires:	gcc, make

%description
This tool lets you monitor I/O latency in real time, in a way
similar to how ping(1) does for network latency.

%prep
%setup -q


%build
CFLAGS="$RPM_OPT_FLAGS" make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install PREFIX=%{_prefix} DESTDIR=$RPM_BUILD_ROOT


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%attr(755,root,root) %{_bindir}/ioping
%attr(644, root, root) %{_mandir}/man1/ioping.1.*
%doc

%changelog
* Mon Aug  1 2011 Konstantin Khlebnikov <koct9i@gmail.com> - 0.6-1
- many fixes
- port to GNU/Hurd

* Thu Jun  16 2011 Konstantin Khlebnikov <koct9i@gmail.com> - 0.5-1
- rate tests
- freebsd and macosx ports

* Thu Jun  2 2011 Konstantin Khlebnikov <koct9i@gmail.com> - 0.4-1
- fix linking

* Thu Jun  2 2011 Kir Kolyshkin <kir@openvz.org> - 0.3-1
- fix i386 build on x86_64

* Mon May 30 2011 Kir Kolyshkin <kir@openvz.org> - 0.1-1
- initial packaging
