Name:		movian
Version:	5.0.1
Release:	1%{?dist}
Summary:	Movian is a Linux based media player.
# movian source manually checked out and put into movian-%{version}.tar.bz2 file
# within rpmbuild environment
Group:		Applications/Multimedia
License:	GPLv3
URL:		https://movian.tv
Source0:	$home/rpmbuild/SOURCES/movian-%{version}.tar.bz2
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	yasm
BuildRequires:	alsa-lib-devel
BuildRequires:	pulseaudio-libs-devel
BuildRequires:	libXScrnSaver-devel
BuildRequires:	libXv-devel
BuildRequires:	avahi-devel
BuildRequires:	libvdpau-devel
BuildRequires:	mesa-libGLw-devel
BuildRequires:	mesa-libGLU-devel
BuildRequires:	librtmp-devel
BuildRequires:	polarssl-devel
BuildRequires:	freetype-devel
BuildRequires:	desktop-file-utils

Requires:	alsa-lib
Requires:	pulseaudio-libs
#Requires:	libXScrnSaver
#Requires:	libXv
Requires:	avahi
#Requires:	libvdpau
Requires:	mesa-libGLw
Requires:	mesa-libGLU
Requires:	librtmp
#Requires:	polarssl
#Requires:	freetype

Requires(post): desktop-file-utils
Requires(postun): desktop-file-utils

%description
OpenGL based Mediaplayer for usage on HTPCs — Listen to Music, watch Photos, play Movies, watch TV, all from within the same application.
Easy to setup, no configuration files. All configuration is tuned from inside the program itself. 


%prep
%setup -q

%build
git checkout release/5.0

# for 64bit path
./configure --release --prefix=/usr --datadir=/usr/share/movian --libdir=/usr/lib64/movian --release

# for 32bit path
#./configure --release --prefix=/usr --datadir=/usr/share/movian --libdir=/usr/lib/movian --release


make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
desktop-file-validate %{buildroot}/%{_datadir}/applications/movian.desktop


%clean
rm -rf $RPM_BUILD_ROOT


%post
scrollkeeper-update -q
update-desktop-database -q
chmod 644 /usr/share/man/man1/movian*
touch %{_datadir}/icons/hicolor
if [ -x /usr/bin/gtk-update-icon-cache ]; then
  /usr/bin/gtk-update-icon-cache --quiet %{_datadir}/icons/hicolor
fi

%postun
scrollkeeper-update -q
update-desktop-database -q
touch %{_datadir}/icons/hicolor
if [ -x /usr/bin/gtk-update-icon-cache ]; then
  /usr/bin/gtk-update-icon-cache --quiet %{_datadir}/icons/hicolor
fi


%files
%defattr(-,root,root,-)
%doc LICENSE README.markdown
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
%{_mandir}/man1/%{name}.1*



%changelog
* Thu  Dec 15 2016 Jonas Karlsson <jonas karlsson at fxdev dot com> 5.0.1-1
- Update for Fedora 25


