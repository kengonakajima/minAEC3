{
    "targets": [
        {
            "target_name": "PA",
            "sources": [ "panode.cpp" ],
            'include_dirs': [
                'pa/include',
            ],
            "conditions": [
                ["OS=='win'", {
                    "libraries": [ "../pa/lib/win/x64/Release/portaudio_x64.lib" ],
                    "link_settings": {
                        "libraries": [
                            
                        ]
                    },            
                }],
                ["OS=='mac'", {
                    "libraries" : [ "../pa/lib/mac/arm64/libportaudio.a"],
                    "xcode_settings": { "MACOSX_DEPLOYMENT_TARGET": "14.0"},
                    "link_settings": {
                        "libraries": [
                            "-framework AudioToolbox"
                        ]
                    },                                
                }]
            ]
        }
    ]
}
