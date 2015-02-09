/*
  +----------------------------------------------------------------------+
  | Yet Another Framework                                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Xinchen Hui  <laruence@php.net>                              |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "zend_smart_str.h" /* for smart_str */
#include "ext/pcre/php_pcre.h" /* for pcre */

#include "php_yaf.h"
#include "yaf_namespace.h"
#include "yaf_exception.h"
#include "yaf_request.h"

#include "yaf_router.h"
#include "routes/yaf_route_interface.h"
#include "routes/yaf_route_rewrite.h"

#include "ext/standard/php_string.h"

zend_class_entry *yaf_route_rewrite_ce;

/** {{{ ARG_INFO
 */
ZEND_BEGIN_ARG_INFO_EX(yaf_route_rewrite_construct_arginfo, 0, 0, 2)
	ZEND_ARG_INFO(0, match)
	ZEND_ARG_ARRAY_INFO(0, route, 0)
	ZEND_ARG_ARRAY_INFO(0, verify, 1)
ZEND_END_ARG_INFO()
/* }}} */

/** {{{ yaf_route_t * yaf_route_rewrite_instance(yaf_route_t *this_ptr, zval *match, zval *router, zval *verify)
 */
yaf_route_t * yaf_route_rewrite_instance(yaf_route_t *this_ptr, zval *match, zval *route, zval *verify) {
	yaf_route_t	*instance;

    instance = this_ptr;
	if (ZVAL_IS_NULL(this_ptr)) {
		object_init_ex(instance, yaf_route_rewrite_ce);
	}

	zend_update_property(yaf_route_rewrite_ce, instance, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_MATCH), match);
	zend_update_property(yaf_route_rewrite_ce, instance, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_ROUTE), route);

	if (!verify) {
		zend_update_property_null(yaf_route_rewrite_ce, instance, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_VERIFY));
	} else {
		zend_update_property(yaf_route_rewrite_ce, instance, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_VERIFY), verify);
	}

	return instance;
}
/* }}} */

/** {{{ static void yaf_route_rewrite_match(yaf_route_t *router, char *uri, int len, zval *ret)
 */
static void yaf_route_rewrite_match(yaf_route_t *router, char *uri, int len, zval *ret) {
	char *seg, *pmatch, *ptrptr;
	int  seg_len;
	zval *match;
	pcre_cache_entry *pce_regexp;
	smart_str pattern = {0};

	if (!len) {
		return;
	}

	match  = zend_read_property(yaf_route_rewrite_ce, router, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_MATCH), 0, NULL);
	pmatch = estrndup(Z_STRVAL_P(match), Z_STRLEN_P(match));

	smart_str_appendc(&pattern, YAF_ROUTE_REGEX_DILIMITER);
	smart_str_appendc(&pattern, '^');

	seg = php_strtok_r(pmatch, YAF_ROUTER_URL_DELIMIETER, &ptrptr);
	while (seg) {
		seg_len = strlen(seg);
		if (seg_len) {
			smart_str_appendl(&pattern, YAF_ROUTER_URL_DELIMIETER, 1);

			if(*(seg) == '*') {
				smart_str_appendl(&pattern, "(?P<__yaf_route_rest>.*)", sizeof("(?P<__yaf_route_rest>.*)") -1);
				break;
			}

			if(*(seg) == ':') {
				smart_str_appendl(&pattern, "(?P<", sizeof("(?P<") -1 );
				smart_str_appendl(&pattern, seg + 1, seg_len - 1);
				smart_str_appendl(&pattern, ">[^"YAF_ROUTER_URL_DELIMIETER"]+)", sizeof(">[^"YAF_ROUTER_URL_DELIMIETER"]+)") - 1);
			} else {
				smart_str_appendl(&pattern, seg, seg_len);
			}

		}
		seg = php_strtok_r(NULL, YAF_ROUTER_URL_DELIMIETER, &ptrptr);
	}

	efree(pmatch);
	smart_str_appendc(&pattern, YAF_ROUTE_REGEX_DILIMITER);
	smart_str_appendc(&pattern, 'i');
	smart_str_0(&pattern);

	if ((pce_regexp = pcre_get_compiled_regex_cache(pattern.s)) == NULL) {
		smart_str_free(&pattern);
		return;
	} else {
		zval matches, subparts;

		smart_str_free(&pattern);

		ZVAL_NULL(&subparts);

		php_pcre_match_impl(pce_regexp, uri, len, &matches, &subparts /* subpats */,
				0/* global */, 0/* ZEND_NUM_ARGS() >= 4 */, 0/*flags PREG_OFFSET_CAPTURE*/, 0/* start_offset */);

		if (!zend_hash_num_elements(Z_ARRVAL(subparts))) {
			zval_ptr_dtor(&subparts);
			return;
		} else {
			zval *pzval;
			zend_string *key;
			ulong idx = 0;
			HashTable *ht;

			array_init(ret);

			ht = Z_ARRVAL(subparts);
			ZEND_HASH_FOREACH_KEY_VAL(ht, idx, key, pzval) {
                if (key) {
                    if (!strncmp(key->val, "__yaf_route_rest", key->len)) {
                        zval args;
                        (void)yaf_router_parse_parameters(Z_STRVAL_P(pzval), &args);
                        if (&args) {
                            zend_hash_copy(Z_ARRVAL_P(ret), Z_ARRVAL(args), (copy_ctor_func_t) zval_add_ref);
                            zval_ptr_dtor(&args);
                        }
                    } else {
                        Z_ADDREF_P(pzval);
                        zend_hash_update(Z_ARRVAL_P(ret), key, pzval);
                    }
                }
			} ZEND_HASH_FOREACH_END();

			zval_ptr_dtor(&subparts);
			return;
		}
	}

	return;
}
/* }}} */

/** {{{ int yaf_route_rewrite_route(yaf_route_t *router, yaf_request_t *request)
 */
int yaf_route_rewrite_route(yaf_route_t *router, yaf_request_t *request) {
	char *request_uri;
	zval *args, *base_uri, *zuri, rargs;

	zuri 	 = zend_read_property(yaf_request_ce, request, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_URI), 1, NULL);
	base_uri = zend_read_property(yaf_request_ce, request, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_BASE), 1, NULL);

	if (base_uri && IS_STRING == Z_TYPE_P(base_uri)
			&& !strncasecmp(Z_STRVAL_P(zuri), Z_STRVAL_P(base_uri), Z_STRLEN_P(base_uri))) {
		request_uri  = estrdup(Z_STRVAL_P(zuri) + Z_STRLEN_P(base_uri));
	} else {
		request_uri  = estrdup(Z_STRVAL_P(zuri));
	}

    ZVAL_NULL(&rargs);
	args = &rargs;
	yaf_route_rewrite_match(router, request_uri, strlen(request_uri), args);
	if (ZVAL_IS_NULL(args)) {
		efree(request_uri);
		return 0;
	} else {
		zval *module, *controller, *action, *routes;

		routes = zend_read_property(yaf_route_rewrite_ce, router, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_ROUTE), 1, NULL);
		if ((module = zend_hash_str_find(Z_ARRVAL_P(routes), ZEND_STRL("module"))) != NULL && IS_STRING == Z_TYPE_P(module)) {
			if (Z_STRVAL_P(module)[0] != ':') {
				zend_update_property(yaf_request_ce, request, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_MODULE), module);
			} else {
				zval *m;
				if ((m = zend_hash_str_find(Z_ARRVAL_P(args), Z_STRVAL_P(module) + 1, Z_STRLEN_P(module) - 1)) != NULL && IS_STRING == Z_TYPE_P(m)) {
					zend_update_property(yaf_request_ce, request, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_MODULE), m);
				}
			}
		}

		if ((controller = zend_hash_str_find(Z_ARRVAL_P(routes), ZEND_STRL("controller"))) != NULL && IS_STRING == Z_TYPE_P(controller)) {
			if (Z_STRVAL_P(controller)[0] != ':') {
				zend_update_property(yaf_request_ce, request, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_CONTROLLER), controller);
			} else {
				zval *c;
				if ((c = zend_hash_str_find(Z_ARRVAL_P(args), Z_STRVAL_P(controller) + 1, Z_STRLEN_P(controller) - 1)) != NULL && IS_STRING == Z_TYPE_P(c)) {
					zend_update_property(yaf_request_ce, request, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_CONTROLLER), c);
				}
			}
		}

		if ((action = zend_hash_str_find(Z_ARRVAL_P(routes), ZEND_STRL("action"))) != NULL && IS_STRING == Z_TYPE_P(action)) {
			if (Z_STRVAL_P(action)[0] != ':') {
				zend_update_property(yaf_request_ce, request, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_ACTION), action);
			} else {
				zval *a;
				if ((a = zend_hash_str_find(Z_ARRVAL_P(args), Z_STRVAL_P(action) + 1, Z_STRLEN_P(action) - 1)) != NULL && IS_STRING == Z_TYPE_P(a)) {
					zend_update_property(yaf_request_ce, request, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_ACTION), a);
				}
			}
		}

		(void)yaf_request_set_params_multi(request, args);
		zval_ptr_dtor(args);
		efree(request_uri);
		return 1;
	}

}
/* }}} */

/** {{{ proto public Yaf_Route_Rewrite::route(Yaf_Request_Abstract $request)
 */
PHP_METHOD(yaf_route_rewrite, route) {
	yaf_route_t 	*route;
	yaf_request_t 	*request;

	route = getThis();

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "O", &request, yaf_request_ce) == FAILURE) {
		return;
	}

	if (!request || IS_OBJECT != Z_TYPE_P(request)
			|| !instanceof_function(Z_OBJCE_P(request), yaf_request_ce)) {
		php_error_docref(NULL, E_WARNING, "Expect a %s instance", yaf_request_ce->name->val);
		RETURN_FALSE;
	}

	RETURN_BOOL(yaf_route_rewrite_route(route, request));
}
/** }}} */

/** {{{ zend_string * yaf_route_rewrite_assemble(yaf_route_t *this_ptr, zval *info, zval *query)
 */
zend_string * yaf_route_rewrite_assemble(yaf_route_t *this_ptr, zval *info, zval *query) {
	zval *match, pidents;
	zval *tmp;
	char *seg, *pmatch, *ptrptr;
	zend_string *key, *inter, *tstr;
	uint seg_len;
	ulong key_idx;
	smart_str squery = {0};
	smart_str wildcard = {0};
	
	array_init(&pidents);

	match  = zend_read_property(yaf_route_rewrite_ce, this_ptr, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_MATCH), 0, NULL);
	pmatch = estrndup(Z_STRVAL_P(match), Z_STRLEN_P(match));
	tstr = zend_string_init(Z_STRVAL_P(match), Z_STRLEN_P(match), 0);
	zend_hash_copy(Z_ARRVAL(pidents), Z_ARRVAL_P(info), (copy_ctor_func_t) zval_add_ref);

	seg = php_strtok_r(pmatch, YAF_ROUTER_URL_DELIMIETER, &ptrptr);	
	while (seg) {
		seg_len = strlen(seg);
		if (seg_len) {
			if(*(seg) == '*') {
				ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL(pidents), key_idx, key, tmp) {
					if (key) {
						if (IS_STRING == Z_TYPE_P(tmp)) {
							smart_str_appendl(&wildcard, key->val + 1, key->len - 1);
							smart_str_appendl(&wildcard, YAF_ROUTER_URL_DELIMIETER, 1);
							smart_str_appendl(&wildcard, Z_STRVAL_P(tmp), Z_STRLEN_P(tmp));
							smart_str_appendl(&wildcard, YAF_ROUTER_URL_DELIMIETER, 1);
						}
					}
				} ZEND_HASH_FOREACH_END();
				smart_str_0(&wildcard);
				inter = php_str_to_str(tstr->val, tstr->len, "*", 1, wildcard.s->val, wildcard.s->len);	
				zend_string_release(tstr);
				tstr = inter;
				break;
			}

			if(*(seg) == ':') {
				if ((tmp = zend_hash_str_find(Z_ARRVAL_P(info), seg, seg_len)) != NULL) {
					inter = php_str_to_str(tstr->val, tstr->len, seg, seg_len, Z_STRVAL_P(tmp), Z_STRLEN_P(tmp));
					zend_string_release(tstr);
					tstr = inter;
					zend_hash_str_del(Z_ARRVAL(pidents), seg, seg_len);
				} 
			} 
		}
		seg = php_strtok_r(NULL, YAF_ROUTER_URL_DELIMIETER, &ptrptr);
	}
	
	smart_str_free(&wildcard);
	efree(pmatch);
	zval_ptr_dtor(&pidents);

	if (query && IS_ARRAY == Z_TYPE_P(query)) {
		HashTable *ht = Z_ARRVAL_P(query);

		smart_str_appendc(&squery, '?');
		ZEND_HASH_FOREACH_KEY_VAL(ht, key_idx, key, tmp) {

			if (key) {
				if (IS_STRING == Z_TYPE_P(tmp)) {
					smart_str_appendl(&squery, key->val, key->len);
					smart_str_appendc(&squery, '=');
					smart_str_appendl(&squery, Z_STRVAL_P(tmp), Z_STRLEN_P(tmp));
					smart_str_appendc(&squery, '&');
				}
			}
		} ZEND_HASH_FOREACH_END();
	}

	if (squery.s->len) {
		uint tmp_len = tstr->len;
		squery.s->len--; /* get rid of the tail & */
		smart_str_0(&squery);
		tstr = zend_string_realloc(tstr, tstr->len + squery.s->len, 0); 
		memcpy(tstr->val + tmp_len, squery.s->val, squery.s->len);
		tstr->val[tstr->len] = '\0';
	}   

	return tstr;
}
/* }}} */

/** {{{ proto public Yaf_Route_Rewrite::match(string $uri)
 */
PHP_METHOD(yaf_route_rewrite, match) {
	char *uri;
	size_t len;
	zval matches;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &uri, &len) == FAILURE) {
		WRONG_PARAM_COUNT;
	}

	if (!len) RETURN_FALSE;

    yaf_route_rewrite_match(getThis(), uri, len, &matches);
    if (ZVAL_IS_NULL(&matches)) {
        RETURN_FALSE;
    }
    RETURN_ZVAL(&matches, 0, 0);
}
/** }}} */

/** {{{ proto public Yaf_Route_Rewrite::__construct(string $match, array $route, array $verify, string $reverse = NULL)
 */
PHP_METHOD(yaf_route_rewrite, __construct) {
	zval 		*match, *route, *verify = NULL;
	yaf_route_t	rself, *self = getThis();

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "za|a", &match, &route, &verify) ==  FAILURE) {
		YAF_UNINITIALIZED_OBJECT(getThis());
		return;
	}

	if (IS_STRING != Z_TYPE_P(match) || !Z_STRLEN_P(match)) {
		YAF_UNINITIALIZED_OBJECT(getThis());
		yaf_trigger_error(YAF_ERR_TYPE_ERROR, "Expects a valid string match as the first parameter");
		RETURN_FALSE;
	}

	if (verify && IS_ARRAY != Z_TYPE_P(verify)) {
		YAF_UNINITIALIZED_OBJECT(getThis());
		yaf_trigger_error(YAF_ERR_TYPE_ERROR, "Expects an array as third parameter",  yaf_route_rewrite_ce->name);
		RETURN_FALSE;
	}

	if (!self) {
	    ZVAL_NULL(&rself);
	    self = &rself;
    }

	(void)yaf_route_rewrite_instance(self, match, route, verify);

	if (self) {
		RETURN_ZVAL(self, 1, 0);
	}

	RETURN_FALSE;
}
/** }}} */

/** {{{ proto public Yaf_Route_rewrite::assemble(array $info[, array $query = NULL])
*/
PHP_METHOD(yaf_route_rewrite, assemble) {
	zval *info, *query = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "a|a", &info, &query) == FAILURE) {
		return;
	} else {
		zend_string *str;
	    if ((str = yaf_route_rewrite_assemble(getThis(), info, query)) != NULL) {
			RETURN_STR(str);
		}
		RETURN_NULL();
	}
}
/* }}} */

/** {{{ yaf_route_rewrite_methods
 */
zend_function_entry yaf_route_rewrite_methods[] = {
	PHP_ME(yaf_route_rewrite, __construct, yaf_route_rewrite_construct_arginfo, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(yaf_route_rewrite, route, yaf_route_route_arginfo, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_route_rewrite, assemble, yaf_route_assemble_arginfo, ZEND_ACC_PUBLIC)
    {NULL, NULL, NULL}
};
/* }}} */

/** {{{ YAF_STARTUP_FUNCTION
 */
YAF_STARTUP_FUNCTION(route_rewrite) {
	zend_class_entry ce;
	YAF_INIT_CLASS_ENTRY(ce, "Yaf_Route_Rewrite", "Yaf\\Route\\Rewrite", yaf_route_rewrite_methods);
	yaf_route_rewrite_ce = zend_register_internal_class_ex(&ce, yaf_route_ce);
	zend_class_implements(yaf_route_rewrite_ce, 1, yaf_route_ce);
	yaf_route_rewrite_ce->ce_flags |= ZEND_ACC_FINAL;

	zend_declare_property_null(yaf_route_rewrite_ce, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_MATCH),  ZEND_ACC_PROTECTED);
	zend_declare_property_null(yaf_route_rewrite_ce, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_ROUTE),  ZEND_ACC_PROTECTED);
	zend_declare_property_null(yaf_route_rewrite_ce, ZEND_STRL(YAF_ROUTE_PROPETY_NAME_VERIFY), ZEND_ACC_PROTECTED);

	return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

