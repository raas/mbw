Summary: Memory bandwidth benchmark
Name: mbw
Version: 1.1
Release: 1
License: LGPL
Buildroot: %{_tmppath}/%{name}-buildroot
Group: System Environment/Base
Source: %{name}.tar.gz
Packager: Andras.Horvath@cern.ch

%description
Test memory copy bandwidth (single thread). Switch off swap or make sure array size does not exceed available free RAM.

%prep
%setup -n %{name}

%build
make

%install
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}%{_mandir}/man1
install -m 755 -o root -g root mbw %{buildroot}/usr/bin/mbw
install -m 644 -o root -g root mbw.1 %{buildroot}%{_mandir}/man1/mbw.1

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
/usr/bin/mbw
%doc
%{_mandir}/man1/mbw.1.gz

%changelog
* Tue Jul 07 2006 Andras Horvath <Andras.Horvath@cern.ch> 1.1-1
- separate array initialization from work -> faster execution
- getopt() options parsing: suppress average, specific tests only, no. runs 
- added quiet mode
- added a new test: memcpy() with given block size

* Thu Apr 26 2006 Andras Horvath <Andras.Horvath@cern.ch> 1.0-1
- initial release
