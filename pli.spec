%global _build_id_links none
Name:           pli
Version:        1.0
Release:        1
Summary:        PL/I to C89 translator
License:        Unspecified (set by the project owner)
URL:            https://github.com/mikebeer/pli
Source0:        pli-%{version}.tar.gz
BuildRequires:  gcc, make

%description
pli translates PL/I source to portable C89. The generated program
includes plirt.h (installed in /usr/share/pli) and is compiled with
any C compiler: cc -I/usr/share/pli prog.c -lm

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags} -std=gnu89" LIBS="-lm -static"

%install
make install DESTDIR=%{buildroot} PREFIX=/usr

%files
/usr/bin/pli
/usr/share/pli

%changelog
* Mon Oct 12 2026 pli project - 1.0-1
- First release
