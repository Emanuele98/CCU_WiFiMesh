/**
 * Node-RED settings for Bumblebee OTA Server
 * Enables HTTP Basic Authentication for dashboard and API endpoints
 */

module.exports = {
    // Enable flow file storage
    flowFile: 'flows.json',
    flowFilePretty: true,
    // Credential encryption
    credentialSecret: process.env.NODE_RED_CREDENTIAL_SECRET || "bumblebee_secret_key_2>
    // ===============================================
    // AUTHENTICATION CONFIGURATION
    // ===============================================
    adminAuth: {
        type: "credentials",
        users: [
            {
                username: "admin",
                password: "$2b$08$CNi/3RnATkuVlB4QhkcyZOYIxpWgR2pgZKJP4Z7CPij5yEkCG1o3q>
                permissions: "*"
            }
        ]
    },
    // Enable HTTP node authentication
    httpNodeAuth: {
        user: "admin",
        pass: "$2b$08$CNi/3RnATkuVlB4QhkcyZOYIxpWgR2pgZKJP4Z7CPij5yEkCG1o3q"  // "bumbl>
    },
    // ===============================================
    // HTTP SETTINGS
    // ===============================================
    uiPort: process.env.PORT || 1880,
    uiHost: "0.0.0.0",
    // Enable HTTPS (optional - currently using HTTP)
    // https: {
    //     key: require("fs").readFileSync('/data/certs/server.key'),
    //     cert: require("fs").readFileSync('/data/certs/server.crt')
    // },
    // ===============================================
    // API SETTINGS
    // ===============================================
    httpAdminRoot: '/',
    httpNodeRoot: '/',
    // CORS settings for dashboard
    httpNodeCors: {
        origin: "*",
        methods: "GET,PUT,POST,DELETE"
    },
    // ===============================================
    // LOGGING
    // ===============================================
    logging: {
        console: {
            level: "info",
            metrics: false,
            audit: false
        }
    },
    // ===============================================
    // EDITOR SETTINGS
    // ===============================================
    editorTheme: {
        page: {
            title: "Bumblebee OTA Server"
        },
        header: {
            title: "Bumblebee OTA",
            image: "/absolute/path/to/bumblebee-logo.png" // Optional logo
        },
        palette: {
            editable: true
        }
    },
    // ===============================================
    // FUNCTION GLOBAL CONTEXT
    // ===============================================
    functionGlobalContext: {
        os: require('os'),
        fs: require('fs'),
        path: require('path'),
        crypto: require('crypto')
    },
    // ===============================================
    // CONTEXT STORAGE
    // ===============================================
    contextStorage: {
        default: {
            module: "localfilesystem",
            config: {
                dir: "/data/context",
                cache: true
            }
        }
    },
    // ===============================================
    // FILE UPLOAD LIMITS
    // ===============================================
    apiMaxLength: '10mb',  // Increased for firmware uploads (~1-2MB typical)

    // ===============================================
    // DISABLE TELEMETRY
    // ===============================================
    disableEditor: false,
    diagnostics: {
        enabled: false
    }
}