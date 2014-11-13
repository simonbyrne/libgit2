#include "clar_libgit2.h"

#include "buffer.h"
#include "path.h"
#include "remote.h"

static const char* tagger_name = "Vicent Marti";
static const char* tagger_email = "vicent@github.com";
static const char* tagger_message = "This is my tag.\n\nThere are many tags, but this one is mine\n";

static int transfer_cb(const git_transfer_progress *stats, void *payload)
{
	int *callcount = (int*)payload;
	GIT_UNUSED(stats);
	(*callcount)++;
	return 0;
}

static void cleanup_local_repo(void *path)
{
	cl_fixture_cleanup((char *)path);
}

void test_network_fetchlocal__complete(void)
{
	git_repository *repo;
	git_remote *origin;
	int callcount = 0;
	git_strarray refnames = {0};

	const char *url = cl_git_fixture_url("testrepo.git");
	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;

	callbacks.transfer_progress = transfer_cb;
	callbacks.payload = &callcount;

	cl_set_cleanup(&cleanup_local_repo, "foo");
	cl_git_pass(git_repository_init(&repo, "foo", true));

	cl_git_pass(git_remote_create(&origin, repo, GIT_REMOTE_ORIGIN, url));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_connect(origin, GIT_DIRECTION_FETCH));
	cl_git_pass(git_remote_download(origin, NULL));
	cl_git_pass(git_remote_update_tips(origin, NULL, NULL));

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(19, (int)refnames.count);
	cl_assert(callcount > 0);

	git_strarray_free(&refnames);
	git_remote_free(origin);
	git_repository_free(repo);
}

void test_network_fetchlocal__prune(void)
{
	git_repository *repo;
	git_remote *origin;
	int callcount = 0;
	git_strarray refnames = {0};
	git_reference *ref;
	git_repository *remote_repo = cl_git_sandbox_init("testrepo.git");
	const char *url = cl_git_path_url(git_repository_path(remote_repo));
	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;

	callbacks.transfer_progress = transfer_cb;
	callbacks.payload = &callcount;

	cl_set_cleanup(&cleanup_local_repo, "foo");
	cl_git_pass(git_repository_init(&repo, "foo", true));

	cl_git_pass(git_remote_create(&origin, repo, GIT_REMOTE_ORIGIN, url));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_fetch(origin, NULL, NULL, NULL));

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(19, (int)refnames.count);
	cl_assert(callcount > 0);
	git_strarray_free(&refnames);
	git_remote_free(origin);

	cl_git_pass(git_reference_lookup(&ref, remote_repo, "refs/heads/br2"));
	cl_git_pass(git_reference_delete(ref));
	git_reference_free(ref);

	cl_git_pass(git_remote_lookup(&origin, repo, GIT_REMOTE_ORIGIN));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_connect(origin, GIT_DIRECTION_FETCH));
	cl_git_pass(git_remote_download(origin, NULL));
	cl_git_pass(git_remote_prune(origin));
	cl_git_pass(git_remote_update_tips(origin, NULL, NULL));

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(18, (int)refnames.count);
	git_strarray_free(&refnames);
	git_remote_free(origin);

	cl_git_pass(git_reference_lookup(&ref, remote_repo, "refs/heads/packed"));
	cl_git_pass(git_reference_delete(ref));
	git_reference_free(ref);

	cl_git_pass(git_remote_lookup(&origin, repo, GIT_REMOTE_ORIGIN));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_connect(origin, GIT_DIRECTION_FETCH));
	cl_git_pass(git_remote_download(origin, NULL));
	cl_git_pass(git_remote_prune(origin));
	cl_git_pass(git_remote_update_tips(origin, NULL, NULL));

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(17, (int)refnames.count);
	git_strarray_free(&refnames);
	git_remote_free(origin);

	git_repository_free(remote_repo);
	git_repository_free(repo);
}

void test_network_fetchlocal__prune_overlapping(void)
{
	git_repository *repo;
	git_remote *origin;
	int callcount = 0;
	git_strarray refnames = {0};
	git_reference *ref;
	git_object *obj;
	git_config *config;

	git_repository *remote_repo = cl_git_sandbox_init("testrepo.git");
	const char *url = cl_git_path_url(git_repository_path(remote_repo));

	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;
	callbacks.transfer_progress = transfer_cb;
	callbacks.payload = &callcount;

	cl_git_pass(git_revparse_single(&obj, remote_repo, "master"));
	cl_git_pass(git_reference_create(&ref, remote_repo, "refs/pull/42/head", git_object_id(obj), 1, NULL, NULL));

	cl_set_cleanup(&cleanup_local_repo, "foo");
	cl_git_pass(git_repository_init(&repo, "foo", true));

	cl_git_pass(git_remote_create(&origin, repo, GIT_REMOTE_ORIGIN, url));
	git_remote_set_callbacks(origin, &callbacks);

	cl_git_pass(git_repository_config(&config, repo));
	cl_git_pass(git_config_set_multivar(config, "remote.origin.fetch", "^$", "refs/pull/*/head:refs/remotes/origin/pr/*"));

	cl_git_pass(git_remote_lookup(&origin, repo, GIT_REMOTE_ORIGIN));
	git_remote_set_prune_refs(origin, 1);
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_fetch(origin, NULL, NULL, NULL));

	cl_git_pass(git_revparse_single(&obj, repo, "origin/master"));
	cl_git_pass(git_revparse_single(&obj, repo, "origin/pr/42"));
	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(20, (int)refnames.count);

	cl_git_pass(git_config_delete_multivar(config, "remote.origin.fetch", "refs"));
	cl_git_pass(git_config_set_multivar(config, "remote.origin.fetch", "^$", "refs/pull/*/head:refs/remotes/origin/pr/*"));
	cl_git_pass(git_config_set_multivar(config, "remote.origin.fetch", "^$", "refs/heads/*:refs/remotes/origin/*"));

	cl_git_pass(git_remote_lookup(&origin, repo, GIT_REMOTE_ORIGIN));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_fetch(origin, NULL, NULL, NULL));

	cl_git_pass(git_revparse_single(&obj, repo, "origin/master"));
	cl_git_pass(git_revparse_single(&obj, repo, "origin/pr/42"));
	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(20, (int)refnames.count);

	cl_git_pass(git_reference_delete(ref));
	cl_git_pass(git_config_delete_multivar(config, "remote.origin.fetch", "refs"));
	cl_git_pass(git_config_set_multivar(config, "remote.origin.fetch", "^$", "refs/heads/*:refs/remotes/origin/*"));

	git_object_free(obj);
	git_config_free(config);
	git_strarray_free(&refnames);
	git_remote_free(origin);
	git_repository_free(remote_repo);
	git_repository_free(repo);
}

void test_network_fetchlocal__fetchprune(void)
{
	git_repository *repo;
	git_remote *origin;
	int callcount = 0;
	git_strarray refnames = {0};
	git_reference *ref;
	git_config *config;
	git_repository *remote_repo = cl_git_sandbox_init("testrepo.git");
	const char *url = cl_git_path_url(git_repository_path(remote_repo));
	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;

	callbacks.transfer_progress = transfer_cb;
	callbacks.payload = &callcount;

	cl_set_cleanup(&cleanup_local_repo, "foo");
	cl_git_pass(git_repository_init(&repo, "foo", true));

	cl_git_pass(git_remote_create(&origin, repo, GIT_REMOTE_ORIGIN, url));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_fetch(origin, NULL, NULL, NULL));

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(19, (int)refnames.count);
	cl_assert(callcount > 0);
	git_strarray_free(&refnames);
	git_remote_free(origin);

	cl_git_pass(git_reference_lookup(&ref, remote_repo, "refs/heads/br2"));
	cl_git_pass(git_reference_delete(ref));
	git_reference_free(ref);

	cl_git_pass(git_remote_lookup(&origin, repo, GIT_REMOTE_ORIGIN));
	git_remote_set_prune_refs(origin, 1);
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_fetch(origin, NULL, NULL, NULL));

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(18, (int)refnames.count);
	git_strarray_free(&refnames);
	git_remote_free(origin);

	cl_git_pass(git_reference_lookup(&ref, remote_repo, "refs/heads/packed"));
	cl_git_pass(git_reference_delete(ref));
	git_reference_free(ref);

	cl_git_pass(git_repository_config(&config, repo));
	cl_git_pass(git_config_set_bool(config, "remote.origin.prune", 1));
	git_config_free(config);
	cl_git_pass(git_remote_lookup(&origin, repo, GIT_REMOTE_ORIGIN));
	cl_assert_equal_i(1, git_remote_prune_refs(origin));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_fetch(origin, NULL, NULL, NULL));

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(17, (int)refnames.count);
	git_strarray_free(&refnames);
	git_remote_free(origin);

	git_repository_free(remote_repo);
	git_repository_free(repo);
}

void test_network_fetchlocal__prune_tag(void)
{
	git_repository *repo;
	git_remote *origin;
	int callcount = 0;
	git_reference *ref;
	git_config *config;
	git_oid tag_id;
	git_signature *tagger;
	git_object *obj;

	git_repository *remote_repo = cl_git_sandbox_init("testrepo.git");
	const char *url = cl_git_path_url(git_repository_path(remote_repo));
	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;

	callbacks.transfer_progress = transfer_cb;
	callbacks.payload = &callcount;

	cl_set_cleanup(&cleanup_local_repo, "foo");
	cl_git_pass(git_repository_init(&repo, "foo", true));

	cl_git_pass(git_remote_create(&origin, repo, GIT_REMOTE_ORIGIN, url));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_fetch(origin, NULL, NULL, NULL));

	cl_git_pass(git_revparse_single(&obj, repo, "origin/master"));

	cl_git_pass(git_reference_create(&ref, repo, "refs/remotes/origin/fake-remote", git_object_id(obj), 1, NULL, NULL));

	/* create signature */
	cl_git_pass(git_signature_new(&tagger, tagger_name, tagger_email, 123456789, 60));

	cl_git_pass(
		git_tag_create(&tag_id, repo,
		  "some-tag", obj, tagger, tagger_message, 0)
	);

	cl_git_pass(git_repository_config(&config, repo));
	cl_git_pass(git_config_set_bool(config, "remote.origin.prune", 1));
	git_config_free(config);
	cl_git_pass(git_remote_lookup(&origin, repo, GIT_REMOTE_ORIGIN));
	cl_assert_equal_i(1, git_remote_prune_refs(origin));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_fetch(origin, NULL, NULL, NULL));

	cl_git_pass(git_revparse_single(&obj, repo, "some-tag"));
	cl_git_fail(git_revparse_single(&obj, repo, "refs/remotes/origin/fake-remote"));

	git_object_free(obj);
	git_remote_free(origin);

	git_repository_free(remote_repo);
	git_repository_free(repo);
}

static void cleanup_sandbox(void *unused)
{
	GIT_UNUSED(unused);
	cl_git_sandbox_cleanup();
}

void test_network_fetchlocal__partial(void)
{
	git_repository *repo = cl_git_sandbox_init("partial-testrepo");
	git_remote *origin;
	int callcount = 0;
	git_strarray refnames = {0};
	const char *url;
	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;

	callbacks.transfer_progress = transfer_cb;
	callbacks.payload = &callcount;

	cl_set_cleanup(&cleanup_sandbox, NULL);
	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(1, (int)refnames.count);

	url = cl_git_fixture_url("testrepo.git");
	cl_git_pass(git_remote_create(&origin, repo, GIT_REMOTE_ORIGIN, url));
	git_remote_set_callbacks(origin, &callbacks);
	cl_git_pass(git_remote_connect(origin, GIT_DIRECTION_FETCH));
	cl_git_pass(git_remote_download(origin, NULL));
	cl_git_pass(git_remote_update_tips(origin, NULL, NULL));

	git_strarray_free(&refnames);

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(20, (int)refnames.count); /* 18 remote + 1 local */
	cl_assert(callcount > 0);

	git_strarray_free(&refnames);
	git_remote_free(origin);
}

static int remote_mirror_cb(git_remote **out, git_repository *repo,
			    const char *name, const char *url, void *payload)
{
	int error;
	git_remote *remote;

	GIT_UNUSED(payload);

	if ((error = git_remote_create(&remote, repo, name, url)) < 0)
		return error;

	git_remote_clear_refspecs(remote);

	if ((error = git_remote_add_fetch(remote, "+refs/*:refs/*")) < 0) {
		git_remote_free(remote);
		return error;
	}

	*out = remote;
	return 0;
}

void test_network_fetchlocal__clone_into_mirror(void)
{
	git_clone_options opts = GIT_CLONE_OPTIONS_INIT;
	git_repository *repo;
	git_reference *head;

	opts.bare = true;
	opts.remote_cb = remote_mirror_cb;
	cl_git_pass(git_clone(&repo, cl_git_fixture_url("testrepo.git"), "./foo.git", &opts));

	cl_git_pass(git_reference_lookup(&head, repo, "HEAD"));
	cl_assert_equal_i(GIT_REF_SYMBOLIC, git_reference_type(head));
	cl_assert_equal_s("refs/heads/master", git_reference_symbolic_target(head));

	git_reference_free(head);
	git_repository_free(repo);
	cl_fixture_cleanup("./foo.git");
}

void test_network_fetchlocal__multi_remotes(void)
{
	git_repository *repo = cl_git_sandbox_init("testrepo.git");
	git_remote *test, *test2;
	git_strarray refnames = {0};
	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;

	cl_set_cleanup(&cleanup_sandbox, NULL);
	callbacks.transfer_progress = transfer_cb;
	cl_git_pass(git_remote_lookup(&test, repo, "test"));
	cl_git_pass(git_remote_set_url(test, cl_git_fixture_url("testrepo.git")));
	git_remote_set_callbacks(test, &callbacks);
	cl_git_pass(git_remote_connect(test, GIT_DIRECTION_FETCH));
	cl_git_pass(git_remote_download(test, NULL));
	cl_git_pass(git_remote_update_tips(test, NULL, NULL));

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(32, (int)refnames.count);
	git_strarray_free(&refnames);

	cl_git_pass(git_remote_lookup(&test2, repo, "test_with_pushurl"));
	cl_git_pass(git_remote_set_url(test2, cl_git_fixture_url("testrepo.git")));
	git_remote_set_callbacks(test2, &callbacks);
	cl_git_pass(git_remote_connect(test2, GIT_DIRECTION_FETCH));
	cl_git_pass(git_remote_download(test2, NULL));
	cl_git_pass(git_remote_update_tips(test2, NULL, NULL));

	cl_git_pass(git_reference_list(&refnames, repo));
	cl_assert_equal_i(44, (int)refnames.count);

	git_strarray_free(&refnames);
	git_remote_free(test);
	git_remote_free(test2);
}
