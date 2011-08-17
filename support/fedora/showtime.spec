Name:		showtime
Version:	3.1.173
Release:	1%{?dist}
Summary:	Showtime is a Linux based media player for usage on HTPC's.
# buildt on fedora 15 with the rpmfusion free/nonfree repositories enabled
# showtime source manually checked out and put into showtime-%{version}.tar.bz2 file
# within rpmbuild environment
Group:		Applications/Multimedia
License:	GPLv3
URL:		http://www.lonelycoder.com/hts
Source0:	$home/rpmbuild/SOURCES/showtime-%{version}.tar.bz2
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	ffmpeg-devel >= 0.7
BuildRequires:	gtk2-devel
BuildRequires:	freetype-devel
BuildRequires:	alsa-lib-devel
BuildRequires:	pulseaudio-libs-devel
BuildRequires:	avahi-devel
BuildRequires:	libcdio-devel
BuildRequires:	libcddb-devel
BuildRequires:	openssl-devel
BuildRequires:	libXv-devel
BuildRequires:	libXScrnSaver-devel
#BuildRequires:	libXNVCtrl-devel
BuildRequires:	mesa-libGL-devel
BuildRequires:	mesa-libGLU-devel
BuildRequires:	desktop-file-utils
BuildRequires:	libvdpau-devel

Requires:       ffmpeg >= 0.7
Requires:	gtk2
Requires:	freetype
Requires:	alsa-lib
Requires:	pulseaudio-libs
Requires:	avahi
Requires:	libcdio
Requires:	libcddb
Requires:	openssl
Requires:	libXv
Requires:	libXScrnSaver
#Requires:	libXNVCtrl
Requires:	mesa-libGL
Requires:	mesa-libGLU
Requires:	libvdpau

Requires(post): desktop-file-utils
Requires(postun): desktop-file-utils

%description
OpenGL based Mediaplayer for usage on HTPCs — Listen to Music, watch Photos, play DVD and Movies, watch TV, all from within the same spiffy application.
Easy to setup, no configuration files. All configuration is tuned from inside the program itself. 



%prep
%setup -q

#
# build now currently depends on the showtime branch of libav git repository 

%build
./configure --release --prefix=/usr --pkg-config-path=${HOME}/libav/lib/pkgconfig
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
desktop-file-validate %{buildroot}/%{_datadir}/applications/showtime.desktop


%clean
rm -rf $RPM_BUILD_ROOT


%post
scrollkeeper-update -q
update-desktop-database -q
chmod 644 /usr/share/man/man1/showtime*
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

### default fedora path
# {_bindir} = /usr/bin
# {_datadir} = /usr/share/
# {_libexecdir} = /usr/libexec/
# {_libdir} = /usr/lib64/    #on 64bit
# {_mandir} = /usr/share/man/

###############################





%changelog
* Wed Aug 17 2011 Jonas Karlsson <Jonas Karlsson at fxdev dot com> 3.1.173
- First rpm on the 3.x branch, compiling against showtime git branch of libav 0.7

* Thu Dec 30 2010 Jonas Karlsson <Jonas Karlsson at fxdev dot com> svn5784
- First RPM (Fedora 14) release


