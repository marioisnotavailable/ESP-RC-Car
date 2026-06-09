---
type: community
cohesion: 0.09
members: 43
---

# REST API File Endpoints

**Cohesion:** 0.09 - loosely connected
**Members:** 43 nodes

## Members
- [[_getHeaderTarget()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[_resolvePathAndTarget()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[_vaultDelete()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[_vaultGet()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[_vaultPatch()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[_vaultPatchTargeted()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[_vaultPatchV2()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[_vaultPatchV3()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[_vaultPost()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[_vaultPut()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[activeFileDelete()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[activeFileGet()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[activeFilePatch()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[activeFilePost()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[activeFilePut()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[certificateGet()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[commandPost()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[errorHandler()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[findHeadingBoundary()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[getDocumentMapObject()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[getFileMetadataObject()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[getPeriodicDateFromParams()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[getPeriodicNoteInterface()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[getResponseMessage()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[getSplicePosition()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[getStatusCode()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[isContentType()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[isPatchOperation()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[isPatchTargetType()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[notFoundHandler()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[periodicDelete()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[periodicGet()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[periodicGetInterface()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[periodicGetNote()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[periodicGetOrCreateNote()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[periodicPatch()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[periodicPost()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[periodicPut()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[redirectToVaultPath()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[returnCannedResponse()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[searchQueryPost()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[toArrayBuffer()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js
- [[waitForFileCache()]] - code - .obsidian/plugins/obsidian-local-rest-api/main.js

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/REST_API_File_Endpoints
SORT file.name ASC
```

## Connections to other communities
- 43 edges to [[_COMMUNITY_Obsidian REST API Routes]]
- 1 edge to [[_COMMUNITY_Auth Middleware]]
- 1 edge to [[_COMMUNITY_Obsidian Graph View Config]]

## Top bridge nodes
- [[returnCannedResponse()]] - degree 29, connects to 3 communities
- [[_vaultPatchTargeted()]] - degree 10, connects to 1 community
- [[redirectToVaultPath()]] - degree 9, connects to 1 community
- [[_resolvePathAndTarget()]] - degree 9, connects to 1 community
- [[_getHeaderTarget()]] - degree 8, connects to 1 community