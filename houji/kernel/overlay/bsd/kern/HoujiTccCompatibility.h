/* iOS 16's libsystem_sandbox uses this exact sentinel for an extension
 * requested with its no-sandbox flag. Its consumer returns handle 0 without
 * a kernel call. SWEET already has no sandbox enforcement; this is not an
 * extension issuer or a replacement for TCC's authorization decision.
 *
 * TCC grants the original Music/Photos entitlement before issuing the
 * extension. Returning ENOENT here changes that authorized reply into an
 * error and prevents the media library from initializing. Limit the native
 * result to tccd's untargeted MediaLibrary and Photos file calls for "/".
 * The original daemon reserves generic calls for Camera/Microphone. Other
 * paths, classes (including PhotosAdd), Mach services, generic and targeted
 * requests retain ENOENT.
 */
#ifndef HOUJI_TCC_COMPATIBILITY_H
#define HOUJI_TCC_COMPATIBILITY_H

static uint64_t houji_tcc_media_issued;
static uint64_t houji_tcc_photos_issued;

static int
houji_tcc_extension_issue(struct proc *p, user_addr_t arg)
{
	/* Actual arm64 iOS 16 ABI: seven 64-bit fields, output is a 2048-byte
	 * userspace allocation. Only the eight-byte sentinel is copied out. */
	struct {
		user_addr_t class_name;
		uint64_t type;
		user_addr_t resource;
		uint64_t flags;
		user_addr_t output;
		int64_t target_pid;
		uint64_t target_version;
	} request;
	static const char media_class[] = "com.apple.tcc.kTCCServiceMediaLibrary";
	static const char photos_class[] = "com.apple.tcc.kTCCServicePhotos";
	static const char unconfined[] = "invalid";
	/* MediaLibrary is the longer of the two exact class names. */
	char path[MAXPATHLEN] = {0}, class_name[sizeof(media_class)], resource[2];
	uint64_t *issued;
	int32_t ignored = 0;
	size_t copied = 0;
	int error;
	if (proc_issetugid(p) || !csproc_get_platform_binary(p)) return ENOENT;
	if (proc_pidpathinfo_internal(p, 0, path, sizeof(path), &ignored) != 0 ||
	    memchr(path, '\0', sizeof(path)) == NULL ||
	    strcmp(path, "/System/Library/PrivateFrameworks/TCC.framework/Support/tccd"))
		return ENOENT;
	if ((error = copyin(arg, &request, sizeof(request))) != 0) return error;
	if (request.type != 0 || !request.resource || request.flags ||
	    request.target_pid || request.target_version || !request.output)
		return ENOENT;
	if ((error = copyinstr(request.class_name, class_name, sizeof(class_name), &copied)) != 0)
		return error;
	if (copied == sizeof(media_class) && !memcmp(class_name, media_class, sizeof(media_class)))
		issued = &houji_tcc_media_issued;
	else if (copied == sizeof(photos_class) && !memcmp(class_name, photos_class, sizeof(photos_class)))
		issued = &houji_tcc_photos_issued;
	else
		return ENOENT;
	if ((error = copyinstr(request.resource, resource, sizeof(resource), &copied)) != 0)
		return error;
	if (copied != sizeof(resource) || resource[0] != '/' || resource[1] != '\0')
		return ENOENT;
	error = copyout(unconfined, request.output, sizeof(unconfined));
	if (!error) __atomic_fetch_add(issued, 1ull, __ATOMIC_RELAXED);
	return error;
}

unsigned
houji_tcc_extension_snapshot(char *out, unsigned capacity)
{
	if (!out || !capacity) return 0;
	int n = snprintf(out, capacity,
	    "HOUJI TCC MEDIA: unconfined-replies=%llu photos-replies=%llu; tccd MediaLibrary/Photos file / only, TCC authorization unchanged\n",
	    (unsigned long long)__atomic_load_n(&houji_tcc_media_issued, __ATOMIC_RELAXED),
	    (unsigned long long)__atomic_load_n(&houji_tcc_photos_issued, __ATOMIC_RELAXED));
	if (n <= 0 || (unsigned)n >= capacity) { out[0] = 0; return 0; }
	return (unsigned)n;
}
#endif
