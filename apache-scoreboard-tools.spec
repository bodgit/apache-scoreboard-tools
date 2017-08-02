Name:		apache-scoreboard-tools
Version:	1.0
Release:	1%{?dist}
Summary:	Tools for checking Apache out-of-band via its scoreboard

Group:		Applications/System
License:	GPL
URL:		http://github.com/bodgit/apache-scoreboard-tools
Source0:	check_apache.c
Source1:	collectd_apache.c
Source2:	Makefile

BuildRequires:	httpd-devel
BuildRequires:	libevent-devel
Requires:	collectd
Requires:	nagios-plugins

Obsoletes:	nagios-apache-scoreboard

%description
Tools for checking Apache out-of-band via its scoreboard


%prep
%setup -cT
%{__cp} %{SOURCE0} .
%{__cp} %{SOURCE1} .
%{__cp} %{SOURCE2} .


%build
make %{?_smp_mflags}


%install
%{__install} -d -m755 %{buildroot}/%{_libdir}/nagios/plugins
%{__install} -m755 check_apache %{buildroot}/%{_libdir}/nagios/plugins/
%{__install} -d -m755 %{buildroot}/%{_sbindir}
%{__install} -m755 collectd_apache %{buildroot}/%{_sbindir}

%files
%doc
%{_libdir}/nagios/plugins/check_apache
%{_sbindir}/collectd_apache


%changelog

