Summary: Complete logging for sudo
Name: sudosh2
Version: 1.0.2
Release: 0
License: OSLv2
Group: Applications/System
URL: http://sudosh2.sourceforge.net
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
Requires: ruby >= 1.8.5

%description
sudosh allows complete session logging of shells run under sudo.

Individual sudo commands are still logged as normal but running a shell under
sudosh records the entire session as well as session timings for complete
playback later.

%prep
%setup -q
%configure --prefix=/usr

%build
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/usr;
cp -r bin %{buildroot}/usr;
chmod -R u=rwx,g=rx,o=rx %{buildroot}/usr
%makeinstall
install -d $RPM_BUILD_ROOT/etc/profile.d/
install -m 0733 -d $RPM_BUILD_ROOT/var/log/sudosh

%clean
rm -rf $RPM_BUILD_ROOT
[ "%{buildroot}" != "/" ] && rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%attr(0755,root,root) %{_bindir}/*
%attr(0755,root,root) /usr/bin/sudosh-syslog-replay
%attr(0644,root,root) %{_mandir}/man1/*
%attr(0644,root,root) %{_mandir}/man5/*
%attr(0644,root,root) %{_mandir}/man8/*

%attr(0555,root,root) %{_sysconfdir}/profile.d/sudosh.sh
%attr(0644,root,root) %{_sysconfdir}/sudosh.conf
%doc AUTHORS COPYING INSTALL NEWS PLATFORMS README ChangeLog
%attr(733,root,root) %dir /var/log/sudosh

%changelog
* Tue Feb 08 2011 Morgan Haskel <mhaskel@onyxpoint.com> - 1.0.2-0
- Updated with all sorts of goodness including:
  - Some code cleanup
  - The ability to dump to syslog
  - The ability to replay from syslog
  - A rate limiter to not collect things that don't seem to be originating from
    a human.

* Tue Mar 01 2005 Chris MacLeod <cmacleod@redhat.com> 
- Initial build.
