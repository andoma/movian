build()
{
    which ccache >/dev/null
    if [ $? -eq 0 ]; then
	echo "Using ccache"
	ccache -s
        export CCACHE_CPP2=yes
        USE_CCACHE="--ccache"
    else
	USE_CCACHE=""
    fi

    ./configure.osx ${RELEASE} --cleanbuild ${USE_CCACHE}
    make ${JARGS} dist
    artifact build.osx/Showtime.dmg dmg application/octet-stream Showtime.dmg

}

clean()
{
    echo No clean needs to be done
}

deps()
{
    echo No deps needs to be done
}

buildenv()
{
    echo No buildenv needs to be done
}

eval $OP
