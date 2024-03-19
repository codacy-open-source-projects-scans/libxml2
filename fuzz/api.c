/*
 * api.c: a libFuzzer target to test all kinds of API functions.
 *
 * See Copyright for the status of this software.
 *
 * This is a simple virtual machine which runs fuzz data as a program.
 *
 * There is a fixed number of registers for basic types like integers
 * or strings as well as libxml2 objects like xmlNode. An opcode
 * typically results in a call to an API function using the freshest
 * registers for each argument type and storing the result in the
 * stalest register. This can be implemented using a ring buffer.
 *
 * There are a few other opcodes to initialize or duplicate registers,
 * so all kinds of API calls can potentially be generated from
 * fuzz data.
 *
 * TODO:
 * - Create documents with a dictionary.
 */

#include <stdlib.h>
#include <string.h>

#define XML_DEPRECATED

#include <libxml/catalog.h>
#include <libxml/HTMLtree.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include "fuzz.h"

#if 0
  #define DEBUG printf
#else
  #define DEBUG(...)
#endif

#define MAX_CONTENT     100
#define MAX_COPY         50

typedef enum {
    /* Basic operations */
    OP_CREATE_INTEGER,
    OP_CREATE_STRING,
    OP_DUP_INTEGER,
    OP_DUP_STRING,
    OP_DUP_NODE,

    /*** tree.h ***/

    /* Tree constructors */
    OP_XML_NEW_DOC,
    OP_XML_NEW_NODE,
    OP_XML_NEW_NODE_EAT_NAME,
    OP_XML_NEW_DOC_NODE,
    OP_XML_NEW_DOC_NODE_EAT_NAME,
    OP_XML_NEW_DOC_RAW_NODE,
    OP_XML_NEW_CHILD,
    OP_XML_NEW_TEXT_CHILD,
    OP_XML_NEW_PROP,
    OP_XML_NEW_DOC_PROP,
    OP_XML_NEW_NS_PROP,
    OP_XML_NEW_NS_PROP_EAT_NAME,
    OP_XML_NEW_TEXT,
    OP_XML_NEW_TEXT_LEN,
    OP_XML_NEW_DOC_TEXT,
    OP_XML_NEW_DOC_TEXT_LEN,
    OP_XML_NEW_PI,
    OP_XML_NEW_DOC_PI,
    OP_XML_NEW_COMMENT,
    OP_XML_NEW_DOC_COMMENT,
    OP_XML_NEW_CDATA_BLOCK,
    OP_XML_NEW_CHAR_REF,
    OP_XML_NEW_REFERENCE,
    OP_XML_NEW_DOC_FRAGMENT,
    OP_XML_CREATE_INT_SUBSET,
    OP_XML_NEW_DTD,

    /* Node copying */
    OP_XML_COPY_DOC,
    OP_XML_COPY_NODE,
    OP_XML_COPY_NODE_LIST,
    OP_XML_DOC_COPY_NODE,
    OP_XML_DOC_COPY_NODE_LIST,
    OP_XML_COPY_PROP,
    OP_XML_COPY_PROP_LIST,
    OP_XML_COPY_DTD,

    /* Node accessors */
    OP_NODE_PARENT,
    OP_NODE_NEXT_SIBLING,
    OP_NODE_PREV_SIBLING,
    OP_NODE_FIRST_CHILD,
    OP_XML_GET_LAST_CHILD,
    OP_NODE_NAME,
    OP_XML_NODE_SET_NAME,
    OP_XML_NODE_GET_CONTENT,
    OP_XML_NODE_SET_CONTENT,
    OP_XML_NODE_SET_CONTENT_LEN,
    OP_XML_NODE_ADD_CONTENT,
    OP_XML_NODE_ADD_CONTENT_LEN,
    OP_XML_GET_INT_SUBSET,
    OP_XML_GET_LINE_NO,
    OP_XML_GET_NODE_PATH,
    OP_XML_DOC_GET_ROOT_ELEMENT,
    OP_XML_DOC_SET_ROOT_ELEMENT,
    OP_XML_NODE_IS_TEXT,
    OP_XML_NODE_GET_ATTR_VALUE,
    OP_XML_NODE_GET_LANG,
    OP_XML_NODE_SET_LANG,
    OP_XML_NODE_GET_SPACE_PRESERVE,
    OP_XML_NODE_SET_SPACE_PRESERVE,
    OP_XML_NODE_GET_BASE,
    OP_XML_NODE_GET_BASE_SAFE,
    OP_XML_NODE_SET_BASE,

    /* Attributes */
    OP_XML_HAS_PROP,
    OP_XML_HAS_NS_PROP,
    OP_XML_GET_PROP,
    OP_XML_GET_NS_PROP,
    OP_XML_GET_NO_NS_PROP,
    OP_XML_SET_PROP,
    OP_XML_SET_NS_PROP,
    OP_XML_REMOVE_PROP,
    OP_XML_UNSET_PROP,
    OP_XML_UNSET_NS_PROP,

    /* Namespaces */
    OP_XML_NEW_NS,
    OP_XML_SEARCH_NS,
    OP_XML_SEARCH_NS_BY_HREF,
    OP_XML_GET_NS_LIST,
    OP_XML_GET_NS_LIST_SAFE,
    OP_XML_SET_NS,
    OP_XML_COPY_NAMESPACE,
    OP_XML_COPY_NAMESPACE_LIST,

    /* Tree manipulation */
    OP_XML_UNLINK_NODE,
    OP_XML_ADD_CHILD,
    OP_XML_ADD_CHILD_LIST,
    OP_XML_REPLACE_NODE,
    OP_XML_ADD_SIBLING,
    OP_XML_ADD_PREV_SIBLING,
    OP_XML_ADD_NEXT_SIBLING,

    /* Misc */
    OP_XML_TEXT_MERGE,
    OP_XML_TEXT_CONCAT,
    OP_XML_STRING_GET_NODE_LIST,
    OP_XML_STRING_LEN_GET_NODE_LIST,
    OP_XML_NODE_LIST_GET_STRING,
    OP_XML_NODE_LIST_GET_RAW_STRING,

    /*** parser.h ***/

    OP_PARSE_DOCUMENT,

    /*** valid.h ***/

    OP_XML_ADD_ELEMENT_DECL,
    OP_XML_ADD_ATTRIBUTE_DECL,
    OP_XML_ADD_NOTATION_DECL,

    OP_XML_GET_DTD_ELEMENT_DESC,
    OP_XML_GET_DTD_QELEMENT_DESC,
    OP_XML_GET_DTD_ATTR_DESC,
    OP_XML_GET_DTD_QATTR_DESC,
    OP_XML_GET_DTD_NOTATION_DESC,

    OP_XML_ADD_ID,
    OP_XML_ADD_ID_SAFE,
    OP_XML_GET_ID,
    OP_XML_IS_ID,
    OP_XML_REMOVE_ID,

    OP_XML_ADD_REF,
    OP_XML_GET_REFS,
    OP_XML_IS_REF,
    OP_XML_REMOVE_REF,

    OP_XML_IS_MIXED_ELEMENT,

    OP_VALIDATE,
    OP_XML_VALIDATE_ATTRIBUTE_VALUE,
    OP_XML_VALIDATE_DTD,
    OP_XML_VALIDATE_NOTATION_USE,

    OP_XML_VALIDATE_NAME_VALUE,
    OP_XML_VALIDATE_NAMES_VALUE,
    OP_XML_VALIDATE_NMTOKEN_VALUE,
    OP_XML_VALIDATE_NMTOKENS_VALUE,

    OP_XML_VALID_NORMALIZE_ATTRIBUTE_VALUE,
    OP_XML_VALID_CTXT_NORMALIZE_ATTRIBUTE_VALUE,
    OP_XML_VALID_GET_POTENTIAL_CHILDREN,
    OP_XML_VALID_GET_VALID_ELEMENTS,

    /*** entities.h ***/

    OP_XML_NEW_ENTITY,
    OP_XML_ADD_ENTITY,
    OP_XML_ADD_DOC_ENTITY,
    OP_XML_ADD_DTD_ENTITY,

    OP_XML_GET_PREDEFINED_ENTITY,
    OP_XML_GET_DOC_ENTITY,
    OP_XML_GET_DTD_ENTITY,
    OP_XML_GET_PARAMETER_ENTITY,

    OP_XML_ENCODE_ENTITIES_REENTRANT,
    OP_XML_ENCODE_SPECIAL_CHARS,

    /*** HTMLtree.h ***/

    OP_HTML_NEW_DOC,
    OP_HTML_NEW_DOC_NO_DTD,
    OP_HTML_GET_META_ENCODING,
    OP_HTML_SET_META_ENCODING,
    OP_HTML_IS_BOOLEAN_ATTR,

    /*** output ***/

    /* string */
    OP_XML_DOC_DUMP_MEMORY,
    OP_XML_DOC_DUMP_MEMORY_ENC,
    OP_XML_DOC_DUMP_FORMAT_MEMORY,
    OP_XML_DOC_DUMP_FORMAT_MEMORY_ENC,
    OP_HTML_DOC_DUMP_MEMORY,
    OP_HTML_DOC_DUMP_MEMORY_FORMAT,

    /* FILE, TODO, use fmemopen */
    OP_XML_DOC_DUMP,
    OP_XML_DOC_FORMAT_DUMP,
    OP_XML_ELEM_DUMP,
    OP_HTML_DOC_DUMP,
    OP_HTML_NODE_DUMP_FILE,
    OP_HTML_NODE_DUMP_FILE_FORMAT,

    /* xmlBuf, no public API */
    OP_XML_BUF_NODE_DUMP,

    /* xmlBuffer */
    OP_XML_NODE_DUMP,
    OP_XML_ATTR_SERIALIZE_TXT_CONTENT,
    OP_XML_DUMP_ELEMENT_DECL,
    OP_XML_DUMP_ELEMENT_TABLE,
    OP_XML_DUMP_ATTRIBUTE_DECL,
    OP_XML_DUMP_ATTRIBUTE_TABLE,
    OP_XML_DUMP_NOTATION_DECL,
    OP_XML_DUMP_NOTATION_TABLE,
    OP_XML_DUMP_ENTITY_DECL,
    OP_XML_DUMP_ENTITIES_TABLE,
    OP_HTML_NODE_DUMP,

    /* xmlOutputBuffer */
    OP_XML_SAVE_FILE_TO,
    OP_XML_SAVE_FORMAT_FILE_TO,
    OP_XML_NODE_DUMP_OUTPUT,
    OP_HTML_DOC_CONTENT_DUMP_OUTPUT,
    OP_HTML_DOC_CONTENT_DUMP_FORMAT_OUTPUT,
    OP_HTML_NODE_DUMP_OUTPUT,
    OP_HTML_NODE_DUMP_FORMAT_OUTPUT,

    /* extra */

    OP_XML_IS_XHTML, /* Misc */
    OP_XML_IS_BLANK_NODE, /* Accessors */
    OP_XML_NODE_BUF_GET_CONTENT, /* output to xmlBuffer */
    OP_XML_BUF_GET_NODE_CONTENT, /* xmlBuf, no public API */

    /* DOM */

    OP_XML_DOM_WRAP_RECONCILE_NAMESPACES,
    OP_XML_DOM_WRAP_ADOPT_NODE,
    OP_XML_DOM_WRAP_REMOVE_NODE,
    OP_XML_DOM_WRAP_CLONE_NODE,
    OP_XML_CHILD_ELEMENT_COUNT,
    OP_XML_FIRST_ELEMENT_CHILD,
    OP_XML_LAST_ELEMENT_CHILD,
    OP_XML_NEXT_ELEMENT_SIBLING,
    OP_XML_PREVIOUS_ELEMENT_SIBLING,

    OP_MAX
} opType;

#define NODE_MASK_TEXT_CONTENT ( \
    (1 << XML_TEXT_NODE) | \
    (1 << XML_CDATA_SECTION_NODE) | \
    (1 << XML_COMMENT_NODE) | \
    (1 << XML_PI_NODE))

#define CHILD_MASK_DOCUMENT ( \
    (1 << XML_ELEMENT_NODE) | \
    (1 << XML_PI_NODE) | \
    (1 << XML_COMMENT_NODE))

#define CHILD_MASK_CONTENT ( \
    (1 << XML_ELEMENT_NODE) | \
    (1 << XML_TEXT_NODE) | \
    (1 << XML_CDATA_SECTION_NODE) | \
    (1 << XML_ENTITY_REF_NODE) | \
    (1 << XML_PI_NODE) | \
    (1 << XML_COMMENT_NODE))

#define CHILD_MASK_ELEMENT ( \
    CHILD_MASK_CONTENT | \
    (1 << XML_ATTRIBUTE_NODE))

#define CHILD_MASK_ATTRIBUTE ( \
    (1 << XML_TEXT_NODE) | \
    (1 << XML_ENTITY_REF_NODE))

#define CHILD_MASK_DTD ( \
    (1 << XML_ELEMENT_DECL) | \
    (1 << XML_ATTRIBUTE_DECL) | \
    (1 << XML_ENTITY_DECL))

static const int childMasks[] = {
    0,
    CHILD_MASK_ELEMENT, /* XML_ELEMENT_NODE */
    CHILD_MASK_ATTRIBUTE, /* XML_ATTRIBUTE_NODE */
    0, /* XML_TEXT_NODE */
    0, /* XML_CDATA_SECTION_NODE */
    0, /* XML_ENTITY_REF_NODE */
    0, /* XML_ENTITY_NODE */
    0, /* XML_PI_NODE */
    0, /* XML_COMMENT_NODE */
    CHILD_MASK_DOCUMENT, /* XML_DOCUMENT_NODE */
    0, /* XML_DOCUMENT_TYPE_NODE */
    CHILD_MASK_CONTENT, /* XML_DOCUMENT_FRAG_NODE */
    0, /* XML_NOTATION_NODE */
    CHILD_MASK_DOCUMENT, /* XML_HTML_DOCUMENT_NODE */
    0, /* XML_DTD_NODE */
    0, /* XML_ELEMENT_DECL */
    0, /* XML_ATTRIBUTE_DECL */
    0, /* XML_ENTITY_DECL */
    0, /* XML_NAMESPACE_DECL */
    0, /* XML_XINCLUDE_START */
    0, /* XML_XINCLUDE_END */
    CHILD_MASK_DOCUMENT /* XML_DOCB_DOCUMENT_NODE */
};

#define REG_MAX 8
#define REG_MASK (REG_MAX - 1)

typedef struct {
    /* Indexes point beyond the most recent item */
    int intIdx;
    int stringIdx;
    int nodeIdx;

    const char *opName;

    /* Registers */
    int integers[REG_MAX];
    xmlChar *strings[REG_MAX];
    xmlNodePtr nodes[REG_MAX];
} xmlFuzzApiVars;

static xmlFuzzApiVars varsStruct;
static xmlFuzzApiVars *const vars = &varsStruct;

/* Debug output */

static void
startOp(const char *name) {
    vars->opName = name;
    DEBUG("%s(", name);
}

static void
endOp(void) {
    DEBUG(" )\n");
}

/* Integers */

static int
getInt(int offset) {
    int idx = (vars->intIdx - offset - 1) & REG_MASK;
    DEBUG(" %d", vars->integers[idx]);
    return vars->integers[idx];
}

static void
setInt(int offset, int n) {
    int idx = (vars->intIdx - offset - 1) & REG_MASK;
    vars->integers[idx] = n;
}

static void
incIntIdx(void) {
    vars->intIdx = (vars->intIdx + 1) & REG_MASK;
}

/* Strings */

static const xmlChar *
getStr(int offset) {
    int idx = (vars->stringIdx - offset - 1) & REG_MASK;
    const xmlChar *str = vars->strings[idx];

    if (str == NULL)
        DEBUG(" NULL");
    else
        DEBUG(" \"%.20s\"", str);

    return str;
}

static const char *
getCStr(int offset) {
    return (const char *) getStr(offset);
}

static void
setStr(int offset, xmlChar *str) {
    xmlChar **strings = vars->strings;
    int idx = (vars->stringIdx - offset - 1) & REG_MASK;
    xmlChar *oldString = strings[idx];

    strings[idx] = str;
    if (oldString)
        xmlFree(oldString);
}

static void
moveStr(int offset, xmlChar *str) {
    if (xmlStrlen(str) > 1000) {
        setStr(offset, NULL);
        xmlFree(str);
    } else {
        setStr(offset, str);
    }
}

/*
 * This doesn't use xmlMalloc and can't fail because of malloc failure
 * injection.
 */
static xmlChar *
uncheckedStrdup(const xmlChar *str) {
    xmlChar *copy;

    if (str == NULL)
        return NULL;

    copy = BAD_CAST strndup((const char *) str, MAX_CONTENT);
    if (copy == NULL) {
        fprintf(stderr, "out of memory\n");
        abort();
    }

    return copy;
}

static void
copyStr(int offset, const xmlChar *str) {
    setStr(offset, uncheckedStrdup(str));
}

static void
incStrIdx(void) {
    vars->stringIdx = (vars->stringIdx + 1) & REG_MASK;
}

/* Nodes */

static void
dropNode(xmlNodePtr node);

static xmlNodePtr
getNode(int offset) {
    int idx = (vars->nodeIdx - offset - 1) & REG_MASK;
    if (vars->nodes[idx])
        DEBUG(" n%d", idx);
    else
        DEBUG(" NULL");
    fflush(stdout);
    return vars->nodes[idx];
}

static xmlDocPtr
getDoc(int offset) {
    xmlNodePtr node = getNode(offset);

    if (node == NULL)
        return NULL;
    return node->doc;
}

static xmlAttrPtr
getAttr(int offset) {
    xmlNodePtr node = getNode(offset);

    if (node == NULL)
        return NULL;
    if (node->type == XML_ATTRIBUTE_NODE)
        return (xmlAttrPtr) node;
    if (node->type == XML_ELEMENT_NODE)
        return node->properties;

    return NULL;
}

static xmlDtdPtr
getDtd(int offset) {
    xmlNodePtr node = getNode(offset);
    xmlDocPtr doc;

    if (node == NULL)
        return NULL;

    if (node->type == XML_DTD_NODE)
        return (xmlDtdPtr) node;

    doc = node->doc;
    if (doc == NULL)
        return NULL;
    if (doc->intSubset != NULL)
        return doc->intSubset;
    return doc->extSubset;
}

static void
setNode(int offset, xmlNodePtr node) {
    int idx = (vars->nodeIdx - offset - 1) & REG_MASK;
    xmlNodePtr oldNode = vars->nodes[idx];

    if (node != oldNode) {
        vars->nodes[idx] = node;
        dropNode(oldNode);
    }

    if (node == NULL)
        DEBUG(" ) /* NULL */\n");
    else
        DEBUG(" ) -> n%d\n", idx);
}

static void
incNodeIdx(void) {
    xmlNodePtr oldNode;
    int idx;

    idx = vars->nodeIdx & REG_MASK;
    vars->nodeIdx = (idx + 1) & REG_MASK;
    oldNode = vars->nodes[idx];

    if (oldNode != NULL) {
        vars->nodes[idx] = NULL;
        dropNode(oldNode);
    }
}

static int
isValidChildType(xmlNodePtr parent, int childType) {
    return ((1 << childType) & childMasks[parent->type]) != 0;
}

static int
isValidChild(xmlNodePtr parent, xmlNodePtr child) {
    xmlNodePtr cur;

    if (child == NULL || parent == NULL)
        return 1;

    if (parent == child)
        return 0;

    if (((1 << child->type) & childMasks[parent->type]) == 0)
        return 0;

    if (child->children == NULL)
        return 1;

    for (cur = parent->parent; cur != NULL; cur = cur->parent)
        if (cur == child)
            return 0;

    return 1;
}

static int
isTextContentNode(xmlNodePtr child) {
    if (child == NULL)
        return 0;

    return ((1 << child->type) & NODE_MASK_TEXT_CONTENT) != 0;
}

static int
isDtdChild(xmlNodePtr child) {
    if (child == NULL)
        return 0;

    return ((1 << child->type) & CHILD_MASK_DTD) != 0;
}

static xmlNodePtr
nodeGetTree(xmlNodePtr node) {
    xmlNodePtr cur = node;

    while (cur->parent)
        cur = cur->parent;
    return cur;
}

/*
 * This function is called whenever a reference to a node is removed.
 * It checks whether the node is still reachable and frees unreferenced
 * nodes.
 *
 * A node is reachable if its tree, identified by the root node,
 * is reachable. If a non-document tree is unreachable, it can be
 * freed.
 *
 * Multiple trees can share the same document, so a document tree
 * can only be freed if no other trees reference the document.
 */
static void
dropNode(xmlNodePtr node) {
    xmlNodePtr *nodes = vars->nodes;
    xmlNodePtr tree;
    xmlDocPtr doc;
    int docReferenced = 0;
    int i;

    if (node == NULL)
        return;

    tree = nodeGetTree(node);
    doc = node->doc;

    for (i = 0; i < REG_MAX; i++) {
        xmlNodePtr other;

        other = nodes[i];
        if (other == NULL)
            continue;

        /*
         * Return if tree is referenced from another node
         */
        if (nodeGetTree(other) == tree)
            return;
        if (doc != NULL && other->doc == doc)
            docReferenced = 1;
    }

    if (tree != (xmlNodePtr) doc && !isDtdChild(tree)) {
        if (doc == NULL || tree->type != XML_DTD_NODE ||
            ((xmlDtdPtr) tree != doc->intSubset &&
             (xmlDtdPtr) tree != doc->extSubset))
            xmlFreeNode(tree);
    }

    /*
     * Also free document if it isn't referenced from other nodes
     */
    if (doc != NULL && !docReferenced)
        xmlFreeDoc(doc);
}

/*
 * removeNode and removeChildren remove all references to a node
 * or its children from the registers. These functions should be
 * called in an API function destroys nodes, for example by merging
 * text nodes.
 */

static void
removeNode(xmlNodePtr node) {
    int i;

    for (i = 0; i < REG_MAX; i++)
        if (vars->nodes[i] == node)
            vars->nodes[i] = NULL;
}

static void
removeChildren(xmlNodePtr parent, int self) {
    int i;

    if (parent == NULL || (!self && parent->children == NULL))
        return;

    for (i = 0; i < REG_MAX; i++) {
        xmlNodePtr node = vars->nodes[i];

        if (node == parent) {
            if (self)
                vars->nodes[i] = NULL;
            continue;
        }

        while (node != NULL) {
            node = node->parent;
            if (node == parent) {
                vars->nodes[i] = NULL;
                break;
            }
        }
    }
}

static xmlNsPtr
nodeGetNs(xmlNodePtr node, int k) {
    int i = 0;
    xmlNsPtr ns, next;

    if (node == NULL || node->type != XML_ELEMENT_NODE)
        return NULL;

    ns = NULL;
    next = node->nsDef;
    while (1) {
        while (next == NULL) {
            node = node->parent;
            if (node == NULL || node->type != XML_ELEMENT_NODE)
                break;
            next = node->nsDef;
        }

        if (next == NULL)
            break;

        ns = next;
        if (i == k)
            break;

        next = ns->next;
        i += 1;
    }

    return ns;
}

/*
 * It's easy for programs to exhibit exponential growth patterns.
 * For example, a tree being copied and added to the original source
 * node doubles memory usage with two operations. Repeating these
 * operations leads to 2^n nodes. Similar issues can arise when
 * concatenating strings.
 *
 * We simply ignore tree copies or truncate text if they grow too
 * large.
 */

static void
checkContent(xmlNodePtr node) {
    if (node != NULL &&
        (node->type == XML_TEXT_NODE ||
         node->type == XML_CDATA_SECTION_NODE ||
         node->type == XML_ENTITY_NODE ||
         node->type == XML_PI_NODE ||
         node->type == XML_COMMENT_NODE ||
         node->type == XML_NOTATION_NODE) &&
        xmlStrlen(node->content) > MAX_CONTENT) {
        xmlNodeSetContent(node, NULL);
        node->content = uncheckedStrdup(BAD_CAST "");
    }
}

static int
countNodes(xmlNodePtr node) {
    xmlNodePtr cur;
    int numNodes;

    if (node == NULL)
        return 0;

    cur = node;
    numNodes = 0;

    while (1) {
        numNodes += 1;

        if (cur->children != NULL &&
            cur->type != XML_ENTITY_REF_NODE) {
            cur = cur->children;
        } else {
            while (cur->next == NULL) {
                if (cur == node)
                    goto done;
                cur = cur->parent;
            }
            cur = cur->next;
        }
    }

done:
    return numNodes;
}

static xmlNodePtr
checkCopy(xmlNodePtr copy) {
    if (countNodes(copy) > MAX_COPY) {
        if (copy->type == XML_DOCUMENT_NODE ||
            copy->type == XML_HTML_DOCUMENT_NODE)
            xmlFreeDoc((xmlDocPtr) copy);
        else
            xmlFreeNode(copy);
        copy = NULL;
    }

    return copy;
}

static int
fixNs(xmlNodePtr node) {
    if (node == NULL)
        return 0;

    if (node->type == XML_ELEMENT_NODE) {
        return xmlReconciliateNs(node->doc, node);
    } else if (node->type == XML_ATTRIBUTE_NODE) {
        xmlNodePtr parent = node->parent;

        if (parent != NULL)
            return xmlReconciliateNs(parent->doc, parent);
        else
            node->ns = NULL;
    }

    return 0;
}

/* Node operations */

static void
opNodeAccessor(int op) {
    xmlNodePtr node;

    switch (op) {
        case OP_NODE_PARENT:
            startOp("parent"); break;
        case OP_NODE_NEXT_SIBLING:
            startOp("next"); break;
        case OP_NODE_PREV_SIBLING:
            startOp("prev"); break;
        case OP_NODE_FIRST_CHILD:
            startOp("children"); break;
        case OP_XML_GET_LAST_CHILD:
            startOp("xmlGetLastChild"); break;
        case OP_XML_GET_INT_SUBSET:
            startOp("xmlGetIntSubset"); break;
        case OP_XML_DOC_GET_ROOT_ELEMENT:
            startOp("xmlDocGetRootElement"); break;
        default:
            break;
    }

    incNodeIdx();
    node = getNode(1);

    if (node != NULL) {
        switch (op) {
            case OP_NODE_PARENT:
                node = node->parent; break;
            case OP_NODE_NEXT_SIBLING:
                node = node->next; break;
            case OP_NODE_PREV_SIBLING:
                node = node->prev; break;
            case OP_NODE_FIRST_CHILD:
                node = node->children; break;
            case OP_XML_GET_LAST_CHILD:
                node = xmlGetLastChild(node); break;
            case OP_XML_GET_INT_SUBSET:
                node = (xmlNodePtr) xmlGetIntSubset(node->doc); break;
            case OP_XML_DOC_GET_ROOT_ELEMENT:
                node = xmlDocGetRootElement(node->doc); break;
            default:
                break;
        }

        /*
         * Don't descend into predefined entities
         */
        if (node != NULL && node->type == XML_ENTITY_DECL) {
            xmlEntityPtr ent = (xmlEntityPtr) node;

            if (ent->etype == XML_INTERNAL_PREDEFINED_ENTITY)
                node = NULL;
        }
    }

    setNode(0, node);
}

static void
opDup(int op) {
    int offset;

    switch (op) {
        case OP_DUP_INTEGER:
            incIntIdx(); break;
        case OP_DUP_STRING:
            incStrIdx(); break;
        case OP_DUP_NODE:
            incNodeIdx(); break;
        default:
            break;
    }

    offset = (xmlFuzzReadInt(1) + 1) & REG_MASK;

    if (offset != 0) {
        startOp("dup");
        switch (op) {
            case OP_DUP_INTEGER:
                setInt(0, getInt(offset));
                endOp();
                break;
            case OP_DUP_STRING:
                copyStr(0, getStr(offset));
                endOp();
                break;
            case OP_DUP_NODE:
                setNode(0, getNode(offset));
                break;
            default:
                break;
        }
    }
}

int
LLVMFuzzerInitialize(int *argc ATTRIBUTE_UNUSED,
                     char ***argv ATTRIBUTE_UNUSED) {
    xmlFuzzMemSetup();
    xmlInitParser();
#ifdef LIBXML_CATALOG_ENABLED
    xmlInitializeCatalog();
    xmlCatalogSetDefaults(XML_CATA_ALLOW_NONE);
#endif
    xmlSetGenericErrorFunc(NULL, xmlFuzzErrorFunc);
    xmlSetExternalEntityLoader(xmlFuzzEntityLoader);

    return 0;
}

int
LLVMFuzzerTestOneInput(const char *data, size_t size) {
    size_t maxAlloc;
    int i;

    memset(vars, 0, sizeof(*vars));

    xmlFuzzDataInit(data, size);

    maxAlloc = xmlFuzzReadInt(4) % (size * 50 + 10);
    xmlFuzzMemSetLimit(maxAlloc);

    while (xmlFuzzBytesRemaining()) {
        size_t readSize;
        int op = xmlFuzzReadInt(1);
        int oomReport = -1;

        vars->opName = "[unset]";

        switch (op) {
            case OP_CREATE_INTEGER:
                incIntIdx();
                setInt(0, (int) xmlFuzzReadInt(4));
                break;

            case OP_CREATE_STRING:
                incStrIdx();
                copyStr(0, BAD_CAST xmlFuzzReadString(&readSize));
                break;

            case OP_DUP_INTEGER:
            case OP_DUP_STRING:
            case OP_DUP_NODE:
                opDup(op);
                break;

            case OP_PARSE_DOCUMENT:
                startOp("xmlReadDoc");
                incNodeIdx();
                setNode(0, (xmlNodePtr) xmlReadDoc(
                    getStr(0),
                    getCStr(1),
                    getCStr(2),
                    getInt(0)));
                break;

            case OP_XML_NEW_DOC: {
                xmlDocPtr doc;

                startOp("xmlNewDoc");
                incNodeIdx();
                doc = xmlNewDoc(getStr(0));
                oomReport = (doc == NULL);
                setNode(0, (xmlNodePtr) doc);
                break;
            }

            case OP_XML_NEW_NODE: {
                xmlNodePtr node;
                const xmlChar *name;

                startOp("xmlNewNode");
                incNodeIdx();
                node = xmlNewNode(
                    nodeGetNs(getNode(1), getInt(0)),
                    name = getStr(0));
                oomReport = (name != NULL && node == NULL);
                if (fixNs(node) < 0)
                    oomReport = 1;
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_NODE_EAT_NAME: {
                xmlNodePtr node;
                xmlChar *name;

                startOp("xmlNewNodeEatName");
                incNodeIdx();
                node = xmlNewNodeEatName(
                    nodeGetNs(getNode(1), getInt(0)),
                    name = uncheckedStrdup(getStr(0)));
                oomReport = (name != NULL && node == NULL);
                if (fixNs(node) < 0)
                    oomReport = 1;
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_DOC_NODE: {
                xmlNodePtr node;
                const xmlChar *name;

                startOp("xmlNewDocNode");
                incNodeIdx();
                node = xmlNewDocNode(
                    getDoc(1),
                    nodeGetNs(getNode(2), getInt(0)),
                    name = getStr(0),
                    getStr(1));
                oomReport = (name != NULL && node == NULL);
                if (fixNs(node) < 0)
                    oomReport = 1;
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_DOC_NODE_EAT_NAME: {
                xmlNodePtr node;
                xmlChar *name;

                startOp("xmlNewDocNodeEatName");
                incNodeIdx();
                node = xmlNewDocNodeEatName(
                    getDoc(1),
                    nodeGetNs(getNode(2), getInt(0)),
                    name = uncheckedStrdup(getStr(0)),
                    getStr(1));
                oomReport = (name != NULL && node == NULL);
                if (fixNs(node) < 0)
                    oomReport = 1;
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_DOC_RAW_NODE: {
                xmlNodePtr node;
                const xmlChar *name;

                startOp("xmlNewDocRawNode");
                incNodeIdx();
                node = xmlNewDocRawNode(
                    getDoc(1),
                    nodeGetNs(getNode(2), getInt(0)),
                    name = getStr(0),
                    getStr(1));
                oomReport = (name != NULL && node == NULL);
                if (fixNs(node) < 0)
                    oomReport = 1;
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_CHILD: {
                xmlNodePtr parent, node;
                const xmlChar *name;

                startOp("xmlNewChild");
                incNodeIdx();
                /* Use parent namespace without fixup */
                node = xmlNewChild(
                    parent = getNode(1),
                    nodeGetNs(getNode(1), getInt(0)),
                    name = getStr(0),
                    getStr(1));
                oomReport =
                    (parent != NULL &&
                     isValidChildType(parent, XML_ELEMENT_NODE) &&
                     name != NULL &&
                     node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_TEXT_CHILD: {
                xmlNodePtr parent, node;
                const xmlChar *name;

                startOp("xmlNewTextChild");
                incNodeIdx();
                /* Use parent namespace without fixup */
                node = xmlNewTextChild(
                    parent = getNode(1),
                    nodeGetNs(getNode(1), getInt(0)),
                    name = getStr(0),
                    getStr(1));
                oomReport =
                    (parent != NULL &&
                     isValidChildType(parent, XML_ELEMENT_NODE) &&
                     name != NULL &&
                     node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_PROP: {
                xmlNodePtr parent;
                xmlAttrPtr attr;
                const xmlChar *name;

                startOp("xmlNewProp");
                incNodeIdx();
                attr = xmlNewProp(
                    parent = getNode(1),
                    name = getStr(0),
                    getStr(1));
                oomReport =
                    ((parent == NULL || parent->type == XML_ELEMENT_NODE) &&
                     name != NULL &&
                     attr == NULL);
                setNode(0, (xmlNodePtr) attr);
                break;
            }

            case OP_XML_NEW_DOC_PROP: {
                xmlAttrPtr attr;
                const xmlChar *name;

                startOp("xmlNewDocProp");
                incNodeIdx();
                attr = xmlNewDocProp(
                    getDoc(1),
                    name = getStr(0),
                    getStr(1));
                oomReport = (name != NULL && attr == NULL);
                setNode(0, (xmlNodePtr) attr);
                break;
            }

            case OP_XML_NEW_NS_PROP: {
                xmlAttrPtr attr;

                startOp("xmlNewNsProp");
                incNodeIdx();
                attr = xmlNewNsProp(
                    getNode(1),
                    nodeGetNs(getNode(1), getInt(0)),
                    getStr(0),
                    getStr(1));
                /* xmlNewNsProp returns NULL on duplicate prefixes. */
                if (attr != NULL)
                    oomReport = 0;
                setNode(0, (xmlNodePtr) attr);
                break;
            }

            case OP_XML_NEW_NS_PROP_EAT_NAME: {
                xmlAttrPtr attr;

                startOp("xmlNewNsPropEatName");
                incNodeIdx();
                attr = xmlNewNsPropEatName(
                    getNode(1),
                    nodeGetNs(getNode(1), getInt(0)),
                    uncheckedStrdup(getStr(0)),
                    getStr(1));
                if (attr != NULL)
                    oomReport = 0;
                setNode(0, (xmlNodePtr) attr);
                break;
            }

            case OP_XML_NEW_TEXT: {
                xmlNodePtr node;

                startOp("xmlNewText");
                incNodeIdx();
                node = xmlNewText(getStr(0));
                oomReport = (node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_TEXT_LEN: {
                xmlNodePtr node;
                const xmlChar *text;

                startOp("xmlNewTextLen");
                incNodeIdx();
                text = getStr(0);
                node = xmlNewTextLen(text, xmlStrlen(text));
                oomReport = (node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_DOC_TEXT: {
                xmlNodePtr node;

                startOp("xmlNewDocText");
                incNodeIdx();
                node = xmlNewDocText(getDoc(1), getStr(0));
                oomReport = (node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_DOC_TEXT_LEN: {
                xmlDocPtr doc;
                xmlNodePtr node;
                const xmlChar *text;

                startOp("xmlNewDocTextLen");
                incNodeIdx();
                doc = getDoc(1);
                text = getStr(0);
                node = xmlNewDocTextLen(doc, text, xmlStrlen(text));
                oomReport = (node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_PI: {
                xmlNodePtr node;
                const xmlChar *name;

                startOp("xmlNewPI");
                incNodeIdx();
                node = xmlNewPI(
                    name = getStr(0),
                    getStr(1));
                oomReport = (name != NULL && node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_DOC_PI: {
                xmlNodePtr node;
                const xmlChar *name;

                startOp("xmlNewDocPI");
                incNodeIdx();
                node = xmlNewDocPI(
                    getDoc(1),
                    name = getStr(0),
                    getStr(1));
                oomReport = (name != NULL && node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_COMMENT: {
                xmlNodePtr node;

                startOp("xmlNewComment");
                incNodeIdx();
                node = xmlNewComment(getStr(0));
                oomReport = (node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_DOC_COMMENT: {
                xmlNodePtr node;

                startOp("xmlNewDocComment");
                incNodeIdx();
                node = xmlNewDocComment(
                    getDoc(1),
                    getStr(0));
                oomReport = (node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_CDATA_BLOCK: {
                xmlDocPtr doc;
                xmlNodePtr node;
                const xmlChar *text;

                startOp("xmlNewCDataBlock");
                incNodeIdx();
                doc = getDoc(1);
                text = getStr(0);
                node = xmlNewDocTextLen(
                    doc,
                    text,
                    xmlStrlen(text));
                oomReport = (node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_CHAR_REF: {
                xmlNodePtr node;
                const xmlChar *name;

                startOp("xmlNewCharRef");
                incNodeIdx();
                node = xmlNewCharRef(
                    getDoc(1),
                    name = getStr(0));
                oomReport = (name != NULL && node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_REFERENCE: {
                xmlNodePtr node;
                const xmlChar *name;

                startOp("xmlNewReference");
                incNodeIdx();
                node = xmlNewReference(
                    getDoc(1),
                    name = getStr(0));
                oomReport = (name != NULL && node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_NEW_DOC_FRAGMENT: {
                xmlNodePtr node;

                startOp("xmlNewDocFragment");
                incNodeIdx();
                node = xmlNewDocFragment(getDoc(1));
                oomReport = (node == NULL);
                setNode(0, node);
                break;
            }

            case OP_XML_CREATE_INT_SUBSET: {
                xmlDocPtr doc;
                xmlDtdPtr dtd = NULL;

                startOp("xmlCreateIntSubset");
                incNodeIdx();
                doc = getDoc(1);
                if (doc == NULL || doc->intSubset == NULL) {
                    dtd = xmlCreateIntSubset(
                        doc,
                        getStr(0),
                        getStr(1),
                        getStr(2));
                    oomReport = (dtd == NULL);
                }
                setNode(0, (xmlNodePtr) dtd);
                break;
            }

            case OP_XML_NEW_DTD: {
                xmlDocPtr doc;
                xmlDtdPtr dtd = NULL;

                startOp("xmlNewDtd");
                incNodeIdx();
                doc = getDoc(1);
                if (doc == NULL || doc->extSubset == NULL) {
                    dtd = xmlNewDtd(
                        doc,
                        getStr(0),
                        getStr(1),
                        getStr(2));
                    oomReport = (dtd == NULL);
                }
                setNode(0, (xmlNodePtr) dtd);
                break;
            }

            case OP_XML_COPY_DOC: {
                xmlDocPtr copy;

                startOp("xmlCopyDoc");
                incNodeIdx();
                copy = xmlCopyDoc(
                    getDoc(1),
                    getInt(0));
                /*
                 * TODO: Copying DTD nodes without a document can
                 * result in an empty list.
                 */
                if (copy != NULL)
                    oomReport = 0;
                setNode(0, checkCopy((xmlNodePtr) copy));
                break;
            }

            case OP_XML_COPY_NODE: {
                xmlNodePtr copy;

                startOp("xmlCopyNode");
                incNodeIdx();
                copy = xmlCopyNode(
                    getNode(1),
                    getInt(0));
                if (copy != NULL)
                    oomReport = 0;
                setNode(0, checkCopy((xmlNodePtr) copy));
                break;
            }

            case OP_XML_COPY_NODE_LIST: {
                xmlNodePtr copy;

                startOp("xmlCopyNodeList");
                copy = xmlCopyNodeList(getNode(0));
                if (copy != NULL)
                    oomReport = 0;
                xmlFreeNodeList(copy);
                endOp();
                break;
            }

            case OP_XML_DOC_COPY_NODE: {
                xmlNodePtr node, copy;
                xmlDocPtr doc;

                startOp("xmlDocCopyNode");
                incNodeIdx();
                copy = xmlDocCopyNode(
                    node = getNode(1),
                    doc = getDoc(2),
                    getInt(0));
                if (copy != NULL)
                    oomReport = 0;
                setNode(0, checkCopy((xmlNodePtr) copy));
                break;
            }

            case OP_XML_DOC_COPY_NODE_LIST: {
                xmlNodePtr copy;

                startOp("xmlDocCopyNodeList");
                copy = xmlDocCopyNodeList(
                    getDoc(0),
                    getNode(1));
                if (copy != NULL)
                    oomReport = 0;
                xmlFreeNodeList(copy);
                endOp();
                break;
            }

            case OP_XML_COPY_PROP: {
                xmlAttrPtr copy;

                startOp("xmlCopyProp");
                incNodeIdx();
                copy = xmlCopyProp(
                    getNode(1),
                    getAttr(2));
                /*
                 * TODO: Copying attributes can result in an empty list
                 * if there's a duplicate namespace prefix.
                 */
                if (copy != NULL)
                    oomReport = 0;
                if (copy != NULL) {
                    /* Quirk */
                    copy->parent = NULL;
                    /* Fix namespace */
                    copy->ns = NULL;
                }
                setNode(0, checkCopy((xmlNodePtr) copy));
                break;
            }

            case OP_XML_COPY_PROP_LIST: {
                xmlAttrPtr copy;

                startOp("xmlCopyPropList");
                copy = xmlCopyPropList(
                    getNode(0),
                    getAttr(1));
                if (copy != NULL)
                    oomReport = 0;
                xmlFreePropList(copy);
                endOp();
                break;
            }

            case OP_XML_COPY_DTD: {
                xmlDtdPtr dtd, copy;

                startOp("xmlCopyDtd");
                incNodeIdx();
                copy = xmlCopyDtd(
                    dtd = getDtd(1));
                oomReport = (dtd != NULL && copy == NULL);
                setNode(0, checkCopy((xmlNodePtr) copy));
                break;
            }

            case OP_NODE_PARENT:
            case OP_NODE_NEXT_SIBLING:
            case OP_NODE_PREV_SIBLING:
            case OP_NODE_FIRST_CHILD:
            case OP_XML_GET_LAST_CHILD:
            case OP_XML_GET_INT_SUBSET:
            case OP_XML_DOC_GET_ROOT_ELEMENT:
                opNodeAccessor(op);
                oomReport = 0;
                break;

            case OP_NODE_NAME: {
                xmlNodePtr node;

                startOp("name");
                incStrIdx();
                node = getNode(0);
                copyStr(0, node ? node->name : NULL);
                oomReport = 0;
                endOp();
                break;
            }

            case OP_XML_NODE_SET_NAME:
                startOp("xmlNodeSetName");
                xmlNodeSetName(
                    getNode(0),
                    getStr(0));
                endOp();
                break;

            case OP_XML_NODE_GET_CONTENT: {
                xmlChar *content;

                incStrIdx();
                startOp("xmlNodeGetContent");
                content = xmlNodeGetContent(getNode(0));
                if (content != NULL)
                    oomReport = 0;
                moveStr(0, content);
                endOp();
                break;
            }

            case OP_XML_NODE_SET_CONTENT: {
                xmlNodePtr node;
                int res;

                startOp("xmlNodeSetContent");
                node = getNode(0);
                removeChildren(node, 0);
                res = xmlNodeSetContent(
                    node,
                    getStr(0));
                oomReport = (res < 0);
                endOp();
                break;
            }

            case OP_XML_NODE_SET_CONTENT_LEN: {
                xmlNodePtr node;
                const xmlChar *content;
                int res;

                startOp("xmlNodeSetContentLen");
                node = getNode(0);
                content = getStr(0);
                removeChildren(node, 0);
                res = xmlNodeSetContentLen(
                    node,
                    content,
                    xmlStrlen(content));
                oomReport = (res < 0);
                endOp();
                break;
            }

            case OP_XML_NODE_ADD_CONTENT: {
                xmlNodePtr node, text;
                int res;

                startOp("xmlNodeAddContent");
                node = getNode(0);
                res = xmlNodeAddContent(
                    node,
                    getStr(0));
                oomReport = (res < 0);
                if (node != NULL) {
                    if (node->type == XML_ELEMENT_NODE ||
                        node->type == XML_DOCUMENT_FRAG_NODE)
                        text = node->last;
                    else
                        text = node;
                    checkContent(text);
                }
                endOp();
                break;
            }

            case OP_XML_NODE_ADD_CONTENT_LEN: {
                xmlNodePtr node, text;
                const xmlChar *content;
                int res;

                startOp("xmlNodeAddContentLen");
                node = getNode(0);
                content = getStr(0);
                res = xmlNodeAddContentLen(
                    node,
                    content,
                    xmlStrlen(content));
                oomReport = res < 0;
                if (node != NULL) {
                    if (node->type == XML_ELEMENT_NODE ||
                        node->type == XML_DOCUMENT_FRAG_NODE)
                        text = node->last;
                    else
                        text = node;
                    checkContent(text);
                }
                endOp();
                break;
            }

            case OP_XML_GET_LINE_NO:
                incIntIdx();
                startOp("xmlGetLineNo");
                setInt(0, xmlGetLineNo(getNode(0)));
                oomReport = 0;
                endOp();
                break;

            case OP_XML_GET_NODE_PATH: {
                xmlChar *path;

                incStrIdx();
                startOp("xmlGetNodePath");
                path = xmlGetNodePath(getNode(0));
                if (path != NULL)
                    oomReport = 0;
                moveStr(0, path);
                endOp();
                break;
            }

            case OP_XML_DOC_SET_ROOT_ELEMENT: {
                xmlDocPtr oldDoc, doc;
                xmlNodePtr oldRoot, oldParent, root;

                startOp("xmlDocSetRootElement");
                incNodeIdx();
                doc = getDoc(1);
                root = getNode(2);
                if (doc != NULL && doc->parent != NULL)
                    doc = NULL;
                oldDoc = root ? root->doc : NULL;
                oldParent = root ? root->parent : NULL;

                oldRoot = xmlDocSetRootElement(doc, root);
                /* We can't really know whether xmlSetTreeDoc failed */
                if (oldRoot != NULL ||
                    root == NULL ||
                    root->doc == oldDoc)
                    oomReport = 0;
                setNode(0, oldRoot);

                if (root &&
                    (root->parent != oldParent ||
                     root->doc != oldDoc)) {
                    if (fixNs(root) < 0)
                        oomReport = 1;
                    if (oldParent != NULL)
                        dropNode(oldParent);
                    else
                        dropNode((xmlNodePtr) oldDoc);
                }
                endOp();
                break;
            }

            case OP_XML_NODE_IS_TEXT:
                incIntIdx();
                startOp("xmlNodeIsText");
                setInt(0, xmlNodeIsText(getNode(0)));
                oomReport = 0;
                endOp();
                break;

            case OP_XML_NODE_GET_ATTR_VALUE: {
                xmlChar *value = NULL;
                int res;

                incStrIdx();
                startOp("xmlNodeGetAttrValue");
                res = xmlNodeGetAttrValue(
                    getNode(0),
                    getStr(1),
                    getStr(2),
                    &value);
                oomReport = (res < 0);
                moveStr(0, value);
                endOp();
                break;
            }

            case OP_XML_NODE_GET_LANG: {
                xmlChar *lang;

                incStrIdx();
                startOp("xmlNodeGetLang");
                lang = xmlNodeGetLang(getNode(0));
                if (lang != NULL)
                    oomReport = 0;
                moveStr(0, lang);
                endOp();
                break;
            }

            case OP_XML_NODE_SET_LANG: {
                xmlNodePtr node;
                xmlAttrPtr attr;
                int res;

                startOp("xmlNodeSetLang");
                node = getNode(0);
                attr = xmlHasNsProp(
                    node,
                    BAD_CAST "lang",
                    XML_XML_NAMESPACE);
                xmlFuzzResetMallocFailed();
                removeChildren((xmlNodePtr) attr, 0);
                res = xmlNodeSetLang(
                    node,
                    getStr(0));
                oomReport = (res < 0);
                endOp();
                break;
            }

            case OP_XML_NODE_GET_SPACE_PRESERVE: {
                int res;

                incIntIdx();
                startOp("xmlNodeGetSpacePreserve");
                res = xmlNodeGetSpacePreserve(getNode(0));
                if (res >= 0)
                    oomReport = 0;
                setInt(0, res);
                endOp();
                break;
            }

            case OP_XML_NODE_SET_SPACE_PRESERVE: {
                xmlNodePtr node;
                xmlAttrPtr attr;
                int res;

                startOp("xmlNodeSetSpacePreserve");
                node = getNode(0);
                attr = xmlHasNsProp(
                    node,
                    BAD_CAST "space",
                    XML_XML_NAMESPACE);
                xmlFuzzResetMallocFailed();
                removeChildren((xmlNodePtr) attr, 0);
                res = xmlNodeSetSpacePreserve(
                    node,
                    getInt(0));
                oomReport = (res < 0);
                endOp();
                break;
            }

            case OP_XML_NODE_GET_BASE: {
                xmlChar *base;

                incStrIdx();
                startOp("xmlNodeGetBase");
                base = xmlNodeGetBase(
                    getDoc(0),
                    getNode(1));
                if (base != NULL)
                    oomReport = 0;
                moveStr(0, base);
                endOp();
                break;
            }

            case OP_XML_NODE_GET_BASE_SAFE: {
                xmlChar *base;
                int res;

                startOp("xmlNodeGetBaseSafe");
                incStrIdx();
                res = xmlNodeGetBaseSafe(
                    getDoc(0),
                    getNode(1),
                    &base);
                oomReport = (res < 0);
                moveStr(0, base);
                endOp();
                break;
            }

            case OP_XML_NODE_SET_BASE: {
                xmlNodePtr node;
                xmlAttrPtr attr;
                int res;

                startOp("xmlNodeSetBase");
                node = getNode(0);
                attr = xmlHasNsProp(
                    node,
                    BAD_CAST "base",
                    XML_XML_NAMESPACE);
                xmlFuzzResetMallocFailed();
                removeChildren((xmlNodePtr) attr, 0);
                res = xmlNodeSetBase(
                    node,
                    getStr(0));
                if (res == 0)
                    oomReport = 0;
                endOp();
                break;
            }

            case OP_XML_IS_BLANK_NODE:
                startOp("xmlIsBlankNode");
                incNodeIdx();
                setInt(0, xmlIsBlankNode(getNode(0)));
                oomReport = 0;
                break;

            case OP_XML_HAS_PROP: {
                xmlNodePtr node;
                xmlAttrPtr attr;

                startOp("xmlHasProp");
                incNodeIdx();
                attr = xmlHasProp(
                    node = getNode(1),
                    getStr(0));
                if (node != NULL &&
                    node->doc != NULL &&
                    node->doc->intSubset != NULL) {
                    /*
                     * xmlHasProp tries to look up default attributes,
                     * requiring a memory allocation which isn't
                     * checked.
                     */
                    if (attr != NULL)
                        oomReport = 0;
                } else {
                    oomReport = 0;
                }
                setNode(0, (xmlNodePtr) attr);
                break;
            }

            case OP_XML_HAS_NS_PROP: {
                xmlNodePtr node;
                xmlAttrPtr attr;

                startOp("xmlHasNsProp");
                incNodeIdx();
                attr = xmlHasNsProp(
                    node = getNode(1),
                    getStr(0),
                    getStr(1));
                if (node != NULL &&
                    node->doc != NULL &&
                    node->doc->intSubset != NULL) {
                    if (attr != NULL)
                        oomReport = 0;
                } else {
                    oomReport = 0;
                }
                setNode(0, (xmlNodePtr) attr);
                break;
            }

            case OP_XML_GET_PROP: {
                xmlChar *content;

                startOp("xmlGetProp");
                incStrIdx();
                content = xmlGetProp(
                    getNode(0),
                    getStr(1));
                if (content != NULL)
                    oomReport = 0;
                moveStr(0, content);
                endOp();
                break;
            }

            case OP_XML_GET_NS_PROP: {
                xmlChar *content;

                startOp("xmlGetNsProp");
                incStrIdx();
                content = xmlGetNsProp(
                    getNode(0),
                    getStr(1),
                    getStr(2));
                if (content != NULL)
                    oomReport = 0;
                moveStr(0, content);
                endOp();
                break;
            }

            case OP_XML_GET_NO_NS_PROP: {
                xmlChar *content;

                startOp("xmlGetNoNsProp");
                incStrIdx();
                content = xmlGetNoNsProp(
                    getNode(0),
                    getStr(1));
                if (content != NULL)
                    oomReport = 0;
                moveStr(0, content);
                endOp();
                break;
            }

            case OP_XML_SET_PROP: {
                xmlNodePtr node;
                xmlAttrPtr oldAttr, attr;
                const xmlChar *name, *value;

                startOp("xmlSetProp");
                incNodeIdx();
                node = getNode(1);
                name = getStr(0);
                value = getStr(1);
                oldAttr = xmlHasProp(node, name);
                xmlFuzzResetMallocFailed();
                if (oldAttr != NULL)
                    removeChildren((xmlNodePtr) oldAttr, 0);
                attr = xmlSetProp(node, name, value);
                oomReport =
                    (node != NULL && node->type == XML_ELEMENT_NODE &&
                     name != NULL &&
                     attr == NULL);
                setNode(0, (xmlNodePtr) attr);
                break;
            }

            case OP_XML_SET_NS_PROP: {
                xmlNodePtr node;
                xmlNsPtr ns;
                xmlAttrPtr oldAttr, attr;
                const xmlChar *name, *value;

                startOp("xmlSetNsProp");
                incNodeIdx();
                node = getNode(1);
                ns = nodeGetNs(getNode(2), getInt(0));
                name = getStr(0);
                value = getStr(1);
                oldAttr = xmlHasNsProp(node, name, ns ? ns->href : NULL);
                xmlFuzzResetMallocFailed();
                if (oldAttr != NULL)
                    removeChildren((xmlNodePtr) oldAttr, 0);
                attr = xmlSetNsProp(node, ns, name, value);
                oomReport =
                    ((node == NULL || node->type == XML_ELEMENT_NODE) &&
                     (ns == NULL || ns->href != NULL) &&
                     name != NULL &&
                     attr == NULL);
                setNode(0, (xmlNodePtr) attr);
                if (ns != NULL) {
                    if (fixNs((xmlNodePtr) attr) < 0)
                        oomReport = 1;
                }
                break;
            }

            case OP_XML_REMOVE_PROP: {
                xmlNodePtr attr, parent = NULL;

                startOp("xmlRemoveProp");
                incIntIdx();
                attr = getNode(0);
                if (attr != NULL) {
                    if (attr->parent != NULL &&
                        attr->type == XML_ATTRIBUTE_NODE)
                        removeChildren(attr, 1);
                    else
                        attr = NULL;
                }
                if (attr != NULL)
                    parent = attr->parent;
                setInt(0, xmlRemoveProp((xmlAttrPtr) attr));
                oomReport = 0;
                dropNode(parent);
                endOp();
                break;
            }

            case OP_XML_UNSET_PROP: {
                xmlNodePtr node;
                xmlAttrPtr attr;
                const xmlChar *name;

                startOp("xmlUnsetProp");
                incIntIdx();
                node = getNode(0);
                name = getStr(0);
                attr = xmlHasNsProp(node, name, NULL);
                xmlFuzzResetMallocFailed();
                if (attr != NULL)
                    removeChildren((xmlNodePtr) attr, 1);
                setInt(0, xmlUnsetProp(node, name));
                oomReport = 0;
                dropNode(node);
                endOp();
                break;
            }

            case OP_XML_UNSET_NS_PROP: {
                xmlNodePtr node;
                xmlNsPtr ns;
                xmlAttrPtr attr;
                const xmlChar *name;

                startOp("xmlUnsetNsProp");
                incIntIdx();
                node = getNode(0);
                ns = nodeGetNs(getNode(1), getInt(1));
                name = getStr(0);
                attr = xmlHasNsProp(node, name, ns ? ns->href : NULL);
                xmlFuzzResetMallocFailed();
                if (attr != NULL)
                    removeChildren((xmlNodePtr) attr, 1);
                setInt(0, xmlUnsetNsProp(node, ns, name));
                oomReport = 0;
                dropNode(node);
                endOp();
                break;
            }

            case OP_XML_NEW_NS: {
                xmlNodePtr node;
                xmlNsPtr ns;

                startOp("xmlNewNs");
                ns = xmlNewNs(
                    node = getNode(0),
                    getStr(0),
                    getStr(1));
                if (ns != NULL)
                    oomReport = 0;
                if (node == NULL)
                    xmlFreeNs(ns);
                endOp();
                break;
            }

            case OP_XML_SEARCH_NS: {
                xmlNsPtr ns;

                startOp("xmlSearchNs");
                ns = xmlSearchNs(
                    getDoc(0),
                    getNode(1),
                    getStr(0));
                if (ns != NULL)
                    oomReport = 0;
                endOp();
                break;
            }

            case OP_XML_SEARCH_NS_BY_HREF: {
                xmlNsPtr ns;

                startOp("xmlSearchNsByHref");
                ns = xmlSearchNsByHref(
                    getDoc(0),
                    getNode(1),
                    getStr(0));
                if (ns != NULL)
                    oomReport = 0;
                endOp();
                break;
            }

            case OP_XML_GET_NS_LIST: {
                xmlNsPtr *list;

                startOp("xmlGetNsList");
                list = xmlGetNsList(
                    getDoc(0),
                    getNode(1));
                if (list != NULL)
                    oomReport = 0;
                xmlFree(list);
                endOp();
                break;
            }

            case OP_XML_GET_NS_LIST_SAFE: {
                xmlNsPtr *list;
                int res;

                startOp("xmlGetNsList");
                res = xmlGetNsListSafe(
                    getDoc(0),
                    getNode(1),
                    &list);
                oomReport = (res < 0);
                xmlFree(list);
                endOp();
                break;
            }

            case OP_XML_SET_NS: {
                xmlNodePtr node;
                xmlNsPtr ns;

                startOp("xmlSetNs");
                node = getNode(0),
                ns = nodeGetNs(getNode(1), getInt(0));
                xmlSetNs(node, ns);
                oomReport = 0;
                if (ns != NULL) {
                    if (fixNs(node) < 0)
                        oomReport = 1;
                }
                endOp();
                break;
            }

            case OP_XML_COPY_NAMESPACE: {
                xmlNsPtr ns, copy;

                startOp("xmlCopyNamespace");
                copy = xmlCopyNamespace(
                    ns = nodeGetNs(getNode(0), getInt(0)));
                oomReport = (ns != NULL && copy == NULL);
                xmlFreeNs(copy);
                endOp();
                break;
            }

            case OP_XML_COPY_NAMESPACE_LIST: {
                xmlNsPtr list, copy;

                startOp("xmlCopyNamespaceList");
                copy = xmlCopyNamespaceList(
                    list = nodeGetNs(getNode(0), getInt(0)));
                oomReport = (list != NULL && copy == NULL);
                xmlFreeNsList(copy);
                endOp();
                break;
            }

            case OP_XML_UNLINK_NODE: {
                xmlNodePtr node, oldParent;
                xmlDocPtr doc;

                startOp("xmlUnlinkNode");
                node = getNode(0);
                doc = node ? node->doc : NULL;
                /*
                 * Unlinking DTD children can cause invalid references
                 * which would be expensive to fix.
                 *
                 * Don't unlink DTD if it is the internal or external
                 * subset of the document.
                 */
                if (node != NULL &&
                    (isDtdChild(node) ||
                     (node->type == XML_DTD_NODE &&
                      doc != NULL &&
                      ((xmlDtdPtr) node == doc->intSubset ||
                       (xmlDtdPtr) node == doc->extSubset))))
                    node = NULL;
                oldParent = node ? node->parent : NULL;
                xmlUnlinkNode(node);
                oomReport = 0;
                if (node != NULL && node->parent != oldParent) {
                    if (fixNs(node) < 0)
                        oomReport = 1;
                    dropNode(oldParent);
                }
                endOp();
                break;
            }

            case OP_XML_REPLACE_NODE: {
                xmlNodePtr old, oldParent, node, oldNodeParent, result;
                xmlDocPtr oldNodeDoc;

                startOp("xmlReplaceNode");
                old = getNode(0);
                node = getNode(1);

                /*
                 * Unlinking DTD children can cause invalid references
                 * which would be expensive to fix.
                 */
                if (isDtdChild(old))
                    old = NULL;
                if (old != NULL && !isValidChild(old->parent, node))
                    node = NULL;

                oldParent = old ? old->parent : NULL;
                oldNodeParent = node ? node->parent : NULL;
                oldNodeDoc = node ? node->doc : NULL;

                result = xmlReplaceNode(old, node);
                oomReport =
                    (old != NULL && old->parent != NULL &&
                     node != NULL &&
                     old != node &&
                     result == NULL);

                if (old != NULL && old->parent != oldParent) {
                    if (fixNs(old) < 0)
                        oomReport = 1;
                }
                if (node == NULL) {
                    /* Old node was unlinked */
                    dropNode(oldParent);
                } else if (node->parent != oldNodeParent ||
                           node->doc != oldNodeDoc) {
                    if (fixNs(node) < 0)
                        oomReport = 1;
                    /* Drop old parent of new node */
                    if (oldNodeParent != NULL)
                        dropNode(oldNodeParent);
                    else
                        dropNode((xmlNodePtr) oldNodeDoc);
                }
                endOp();
                break;
            }

            case OP_XML_ADD_CHILD:
            case OP_XML_ADD_SIBLING:
            case OP_XML_ADD_PREV_SIBLING:
            case OP_XML_ADD_NEXT_SIBLING: {
                xmlNodePtr target, parent, node, oldNodeParent, result;
                xmlDocPtr oldNodeDoc;
                int argsOk;

                switch (op) {
                    case OP_XML_ADD_CHILD:
                        startOp("xmlAddChild"); break;
                    case OP_XML_ADD_SIBLING:
                        startOp("xmlAddSibling"); break;
                    case OP_XML_ADD_PREV_SIBLING:
                        startOp("xmlAddPrevSibling"); break;
                    case OP_XML_ADD_NEXT_SIBLING:
                        startOp("xmlAddNextSibling"); break;
                }

                if (op == OP_XML_ADD_CHILD) {
                    target = NULL;
                    parent = getNode(0);
                } else {
                    target = getNode(0);
                    parent = target ? target->parent : NULL;
                }
                node = getNode(1);

                /* Don't append to root node */
                if (target != NULL && parent == NULL)
                    node = NULL;

                /* Check tree structure */
                if (isDtdChild(node) ||
                    !isValidChild(parent, node))
                    node = NULL;

                /* Attributes */
                if (node != NULL && node->type == XML_ATTRIBUTE_NODE) {
                    if ((op == OP_XML_ADD_CHILD) ||
                        ((target != NULL &&
                         (target->type == XML_ATTRIBUTE_NODE)))) {
                        xmlAttrPtr attr = xmlHasNsProp(parent, node->name,
                            node->ns ? node->ns->href : NULL);

                        xmlFuzzResetMallocFailed();
                        /* Attribute might be replaced */
                        if (attr != NULL && attr != (xmlAttrPtr) node)
                            removeChildren((xmlNodePtr) attr, 1);
                    } else {
                        target = NULL;
                    }
                } else if (target != NULL &&
                           target->type == XML_ATTRIBUTE_NODE) {
                    node = NULL;
                }

                oldNodeParent = node ? node->parent : NULL;
                oldNodeDoc = node ? node->doc : NULL;
                argsOk =
                    (target != NULL &&
                     node != NULL &&
                     target != node);

                switch (op) {
                    case OP_XML_ADD_CHILD:
                        argsOk =
                            (parent != NULL &&
                             node != NULL &&
                             node->next == NULL &&
                             node->prev == NULL &&
                             (node->parent == NULL ||
                              node->parent == parent));
                        result = xmlAddChild(parent, node);
                        break;
                    case OP_XML_ADD_SIBLING:
                        result = xmlAddSibling(target, node);
                        break;
                    case OP_XML_ADD_PREV_SIBLING:
                        result = xmlAddPrevSibling(target, node);
                        break;
                    case OP_XML_ADD_NEXT_SIBLING:
                        result = xmlAddNextSibling(target, node);
                        break;
                }
                oomReport = (argsOk && result == NULL);

                if (result != NULL && result != node) {
                    /* Text node was merged */
                    removeNode(node);
                    /* Drop old parent of node */
                    if (oldNodeParent != NULL)
                        dropNode(oldNodeParent);
                    else
                        dropNode((xmlNodePtr) oldNodeDoc);
                } else if (node != NULL &&
                           (node->parent != oldNodeParent ||
                            node->doc != oldNodeDoc)) {
                    if (fixNs(node) < 0)
                        oomReport = 1;
                    /* Drop old parent of node */
                    if (oldNodeParent != NULL)
                        dropNode(oldNodeParent);
                    else
                        dropNode((xmlNodePtr) oldNodeDoc);
                }

                endOp();
                break;
            }

            case OP_XML_TEXT_MERGE: {
                xmlNodePtr first, second, parent = NULL, res;
                int argsOk;

                startOp("xmlTextMerge");
                first = getNode(0);
                second = getNode(1);
                argsOk =
                    (first != NULL && first->type == XML_TEXT_NODE &&
                     second != NULL && second->type == XML_TEXT_NODE &&
                     first != second &&
                     first->name == second->name);
                if (argsOk) {
                    if (second->parent != NULL)
                        parent = second->parent;
                    else
                        parent = (xmlNodePtr) second->doc;

                }
                res = xmlTextMerge(first, second);
                oomReport = (argsOk && res == NULL);
                if (res != NULL) {
                    removeNode(second);
                    dropNode(parent);
                    checkContent(first);
                }
                endOp();
                break;
            }

            case OP_XML_TEXT_CONCAT: {
                xmlNodePtr node;
                const xmlChar *text;
                int res;

                startOp("xmlTextConcat");
                node = getNode(0);
                text = getStr(0);
                res = xmlTextConcat(
                    node,
                    text,
                    xmlStrlen(text));
                oomReport = (isTextContentNode(node) && res < 0);
                checkContent(node);
                endOp();
                break;
            }

            case OP_XML_STRING_GET_NODE_LIST: {
                xmlNodePtr list;
                const xmlChar *value;

                startOp("xmlStringGetNodeList");
                list = xmlStringGetNodeList(
                    getDoc(0),
                    value = getStr(0));
                oomReport = (value != NULL && list == NULL);
                xmlFreeNodeList(list);
                endOp();
                break;
            }

            case OP_XML_STRING_LEN_GET_NODE_LIST: {
                xmlDocPtr doc;
                xmlNodePtr list;
                const xmlChar *value;

                startOp("xmlStringLenGetNodeList");
                doc = getDoc(0);
                value = getStr(0);
                list = xmlStringLenGetNodeList(
                    doc,
                    value,
                    xmlStrlen(value));
                oomReport = (value != NULL && list == NULL);
                xmlFreeNodeList(list);
                endOp();
                break;
            }

            case OP_XML_NODE_LIST_GET_STRING: {
                xmlChar *string;

                startOp("xmlNodeListGetString");
                incStrIdx();
                string = xmlNodeListGetString(
                    getDoc(0),
                    getNode(1),
                    getInt(0));
                oomReport = (string == NULL);
                moveStr(0, string);
                endOp();
                break;
            }

            case OP_XML_NODE_LIST_GET_RAW_STRING: {
                xmlChar *string;

                startOp("xmlNodeListGetRawString");
                incStrIdx();
                string = xmlNodeListGetRawString(
                    getDoc(0),
                    getNode(1),
                    getInt(0));
                oomReport = (string == NULL);
                moveStr(0, string);
                endOp();
                break;
            }

            case OP_XML_IS_XHTML:
                startOp("xmlIsXHTML");
                incIntIdx();
                setInt(0, xmlIsXHTML(
                    getStr(0),
                    getStr(1)));
                oomReport = 0;
                break;

            case OP_XML_ADD_ELEMENT_DECL: {
                xmlElementPtr decl;

                startOp("xmlAddElementDecl");
                incNodeIdx();
                decl = xmlAddElementDecl(
                    NULL,
                    getDtd(1),
                    getStr(0),
                    (xmlElementTypeVal) getInt(0),
                    NULL);
                if (decl != NULL)
                    oomReport = 0;
                setNode(0, (xmlNodePtr) decl);
                break;
            }

            case OP_XML_ADD_ATTRIBUTE_DECL: {
                xmlAttributePtr decl;

                startOp("xmlAddAttributeDecl");
                incNodeIdx();
                decl = xmlAddAttributeDecl(
                    NULL,
                    getDtd(1),
                    getStr(0),
                    getStr(1),
                    getStr(2),
                    (xmlAttributeType) getInt(0),
                    (xmlAttributeDefault) getInt(1),
                    getStr(3),
                    NULL);
                if (decl != NULL)
                    oomReport = 0;
                setNode(0, (xmlNodePtr) decl);
                break;
            }

            case OP_XML_ADD_NOTATION_DECL: {
                xmlNotationPtr decl;

                startOp("xmlAddNotationDecl");
                decl = xmlAddNotationDecl(
                    NULL,
                    getDtd(1),
                    getStr(0),
                    getStr(1),
                    getStr(2));
                if (decl != NULL)
                    oomReport = 0;
                endOp();
                break;
            }

            case OP_XML_GET_DTD_ELEMENT_DESC: {
                xmlElementPtr elem;

                startOp("xmlGetDtdElementDesc");
                incNodeIdx();
                elem = xmlGetDtdElementDesc(
                    getDtd(1),
                    getStr(0));
                if (elem != NULL)
                    oomReport = 0;
                /*
                 * Don't reference XML_ELEMENT_TYPE_UNDEFINED dummy
                 * declarations.
                 */
                if (elem != NULL && elem->parent == NULL)
                    elem = NULL;
                setNode(0, (xmlNodePtr) elem);
                break;
            }

            case OP_XML_GET_DTD_QELEMENT_DESC: {
                xmlElementPtr elem;

                startOp("xmlGetDtdQElementDesc");
                incNodeIdx();
                elem = xmlGetDtdQElementDesc(
                    getDtd(1),
                    getStr(0),
                    getStr(1));
                oomReport = 0;
                if (elem != NULL && elem->parent == NULL)
                    elem = NULL;
                setNode(0, (xmlNodePtr) elem);
                break;
            }

            case OP_XML_GET_DTD_ATTR_DESC: {
                xmlAttributePtr decl;

                startOp("xmlGetDtdAttrDesc");
                incNodeIdx();
                decl = xmlGetDtdAttrDesc(
                    getDtd(1),
                    getStr(0),
                    getStr(1));
                if (decl != NULL)
                    oomReport = 0;
                setNode(0, (xmlNodePtr) decl);
                break;
            }

            case OP_XML_GET_DTD_QATTR_DESC: {
                xmlAttributePtr decl;

                startOp("xmlGetDtdQAttrDesc");
                incNodeIdx();
                decl = xmlGetDtdQAttrDesc(
                    getDtd(1),
                    getStr(0),
                    getStr(1),
                    getStr(2));
                oomReport = 0;
                setNode(0, (xmlNodePtr) decl);
                break;
            }

            case OP_XML_GET_DTD_NOTATION_DESC:
                startOp("xmlGetDtdNotationDesc");
                xmlGetDtdNotationDesc(
                    getDtd(1),
                    getStr(0));
                oomReport = 0;
                endOp();
                break;

            case OP_XML_ADD_ID:
                startOp("xmlAddID");
                xmlAddID(
                    NULL,
                    getDoc(0),
                    getStr(0),
                    getAttr(1));
                endOp();
                break;

            case OP_XML_ADD_ID_SAFE: {
                int res;

                startOp("xmlAddIDSafe");
                res = xmlAddIDSafe(
                    getAttr(0),
                    getStr(0));
                oomReport = (res < 0);
                endOp();
                break;
            }

            case OP_XML_GET_ID:
                startOp("xmlGetID");
                incNodeIdx();
                setNode(0, (xmlNodePtr) xmlGetID(
                    getDoc(1),
                    getStr(0)));
                oomReport = 0;
                break;

            case OP_XML_IS_ID: {
                int res;

                startOp("xmlIsID");
                res = xmlIsID(
                    getDoc(2),
                    getNode(1),
                    getAttr(0));
                oomReport = (res < 0);
                endOp();
                break;
            }

            case OP_XML_REMOVE_ID:
                startOp("xmlRemoveID");
                xmlRemoveID(
                    getDoc(1),
                    getAttr(0));
                oomReport = 0;
                endOp();
                break;

            case OP_XML_ADD_REF: {
                xmlDocPtr doc;
                xmlAttrPtr attr;
                xmlRefPtr ref;
                const xmlChar *value;

                startOp("xmlAddRef");
                ref = xmlAddRef(
                    NULL,
                    doc = getDoc(0),
                    value = getStr(0),
                    attr = getAttr(1));
                oomReport =
                    (doc != NULL &&
                     value != NULL &&
                     attr != NULL &&
                     ref == NULL);
                endOp();
                break;
            }

            case OP_XML_GET_REFS:
                startOp("xmlGetRefs");
                xmlGetRefs(
                    getDoc(1),
                    getStr(0));
                oomReport = 0;
                endOp();
                break;

            case OP_XML_IS_REF:
                startOp("xmlIsRef");
                xmlIsRef(
                    getDoc(2),
                    getNode(1),
                    getAttr(0));
                oomReport = 0;
                endOp();
                break;

            case OP_XML_REMOVE_REF: {
                int res;

                startOp("xmlRemoveRef");
                res = xmlRemoveRef(
                    getDoc(1),
                    getAttr(0));
                if (res == 0)
                    oomReport = 0;
                endOp();
                break;
            }

            case OP_XML_NEW_ENTITY: {
                xmlDocPtr doc;
                xmlEntityPtr ent;

                startOp("xmlNewEntity");
                incNodeIdx();
                ent = xmlNewEntity(
                    doc = getDoc(1),
                    getStr(0),
                    getInt(0),
                    getStr(1),
                    getStr(2),
                    getStr(3));
                if (ent != NULL)
                    oomReport = 0;
                if (doc == NULL || doc->intSubset == NULL) {
                    xmlFreeEntity(ent);
                    ent = NULL;
                }
                setNode(0, (xmlNodePtr) ent);
                break;
            }

            case OP_XML_ADD_ENTITY: {
                xmlEntityPtr ent;
                int res;

                startOp("xmlAddEntity");
                incNodeIdx();
                res = xmlAddEntity(
                    getDoc(1),
                    getInt(0),
                    getStr(0),
                    getInt(1),
                    getStr(1),
                    getStr(2),
                    getStr(3),
                    &ent);
                oomReport = (res == XML_ERR_NO_MEMORY);
                setNode(0, (xmlNodePtr) ent);
                break;
            }

            case OP_XML_ADD_DOC_ENTITY: {
                xmlEntityPtr ent;

                startOp("xmlAddDocEntity");
                incNodeIdx();
                ent = xmlAddDocEntity(
                    getDoc(1),
                    getStr(0),
                    getInt(1),
                    getStr(1),
                    getStr(2),
                    getStr(3));
                if (ent != NULL)
                    oomReport = 0;
                setNode(0, (xmlNodePtr) ent);
                break;
            }

            case OP_XML_ADD_DTD_ENTITY: {
                xmlEntityPtr ent;

                startOp("xmlAddDtdEntity");
                incNodeIdx();
                ent = xmlAddDtdEntity(
                    getDoc(1),
                    getStr(0),
                    getInt(1),
                    getStr(1),
                    getStr(2),
                    getStr(3));
                setNode(0, (xmlNodePtr) ent);
                break;
            }

            case OP_XML_GET_PREDEFINED_ENTITY:
                startOp("xmlGetPredefinedEntity");
                incNodeIdx();
                setNode(0, (xmlNodePtr) xmlGetPredefinedEntity(
                    getStr(0)));
                oomReport = 0;
                break;

            case OP_XML_GET_DOC_ENTITY:
                startOp("xmlGetDocEntity");
                incNodeIdx();
                setNode(0, (xmlNodePtr) xmlGetDocEntity(
                    getDoc(1),
                    getStr(0)));
                oomReport = 0;
                break;

            case OP_XML_GET_DTD_ENTITY:
                startOp("xmlGetDtdEntity");
                incNodeIdx();
                setNode(0, (xmlNodePtr) xmlGetDtdEntity(
                    getDoc(1),
                    getStr(0)));
                oomReport = 0;
                break;

            case OP_XML_GET_PARAMETER_ENTITY:
                startOp("xmlGetParameterEntity");
                incNodeIdx();
                setNode(0, (xmlNodePtr) xmlGetParameterEntity(
                    getDoc(1),
                    getStr(0)));
                oomReport = 0;
                break;

            case OP_XML_ENCODE_ENTITIES_REENTRANT: {
                const xmlChar *string;
                xmlChar *encoded;

                startOp("xmlEncodeEntitiesReentrant");
                incStrIdx();
                encoded = xmlEncodeEntitiesReentrant(
                    getDoc(0),
                    string = getStr(1));
                oomReport = (string != NULL && encoded == NULL);
                moveStr(0, encoded);
                endOp();
                break;
            }

            case OP_XML_ENCODE_SPECIAL_CHARS: {
                const xmlChar *string;
                xmlChar *encoded;

                startOp("xmlEncodespecialChars");
                incStrIdx();
                encoded = xmlEncodeSpecialChars(
                    getDoc(0),
                    string = getStr(1));
                oomReport = (string != NULL && encoded == NULL);
                moveStr(0, encoded);
                endOp();
                break;
            }

#ifdef LIBXML_HTML_ENABLED
            case OP_HTML_NEW_DOC: {
                htmlDocPtr doc;

                startOp("htmlNewDoc");
                incNodeIdx();
                doc = htmlNewDoc(
                    getStr(0),
                    getStr(1));
                oomReport = (doc == NULL);
                setNode(0, (xmlNodePtr) doc);
                break;
            }

            case OP_HTML_NEW_DOC_NO_DTD: {
                htmlDocPtr doc;

                startOp("htmlNewDocNoDtD");
                incNodeIdx();
                doc = htmlNewDocNoDtD(
                    getStr(0),
                    getStr(1));
                oomReport = (doc == NULL);
                setNode(0, (xmlNodePtr) doc);
                break;
            }

            case OP_HTML_GET_META_ENCODING: {
                const xmlChar *encoding;

                startOp("htmlGetMetaEncoding");
                incStrIdx();
                encoding = htmlGetMetaEncoding(getDoc(0));
                if (encoding != NULL)
                    oomReport = 0;
                copyStr(0, encoding);
                break;
            }

            case OP_HTML_SET_META_ENCODING:
                /* TODO (can destroy inner text) */
                break;

            case OP_HTML_IS_BOOLEAN_ATTR:
                startOp("htmlIsBooleanAttr");
                htmlIsBooleanAttr(getStr(0));
                oomReport = 0;
                endOp();
                break;
#endif

#ifdef LIBXML_VALID_ENABLED
            case OP_VALIDATE: {
                xmlNodePtr node;
                int type;
                int res = 1;

                startOp("validate");
                incIntIdx();
                node = getNode(0);
                type = node ? node->type : 0;
                xmlValidCtxtPtr vctxt = xmlNewValidCtxt();
                xmlFuzzResetMallocFailed();

                switch (type) {
                    case XML_DOCUMENT_NODE:
                    case XML_HTML_DOCUMENT_NODE:
                        res = xmlValidateDocument(vctxt, (xmlDocPtr) node);
                        break;
                    case XML_ELEMENT_DECL:
                        res = xmlValidateElementDecl(vctxt, node->doc,
                            (xmlElementPtr) node);
                        break;
                    case XML_ATTRIBUTE_DECL:
                        res = xmlValidateAttributeDecl(vctxt, node->doc,
                            (xmlAttributePtr) node);
                        break;
                    case XML_ELEMENT_NODE:
                        res = xmlValidateElement(vctxt, node->doc, node);
                        break;
                    default:
                        break;
                }

                if (res != 0)
                    oomReport = 0;
                xmlFreeValidCtxt(vctxt);
                setInt(0, res);
                endOp();
                break;
            }

            case OP_XML_VALIDATE_DTD: {
                xmlValidCtxtPtr vctxt;
                int res;

                startOp("xmlValidateDtd");
                incIntIdx();
                vctxt = xmlNewValidCtxt();
                res = xmlValidateDtd(
                    vctxt,
                    getDoc(0),
                    getDtd(1));
                if (res != 0)
                    oomReport = 0;
                xmlFreeValidCtxt(vctxt);
                setInt(0, res);
                endOp();
                break;
            }
#endif /* LIBXML_VALID_ENABLED */

#ifdef LIBXML_OUTPUT_ENABLED
            case OP_XML_DOC_DUMP_MEMORY:
            case OP_XML_DOC_DUMP_MEMORY_ENC:
            case OP_XML_DOC_DUMP_FORMAT_MEMORY:
            case OP_XML_DOC_DUMP_FORMAT_MEMORY_ENC:
            case OP_HTML_DOC_DUMP_MEMORY:
            case OP_HTML_DOC_DUMP_MEMORY_FORMAT: {
                xmlDocPtr doc;
                xmlChar *out = NULL;
                int outSize = 0;

                switch (op) {
                    case OP_XML_DOC_DUMP_MEMORY:
                        startOp("xmlDocDumpMemory"); break;
                    case OP_XML_DOC_DUMP_MEMORY_ENC:
                        startOp("xmlDocDumpMemoryEnc"); break;
                    case OP_XML_DOC_DUMP_FORMAT_MEMORY:
                        startOp("xmlDocDumpFormatMemory"); break;
                    case OP_XML_DOC_DUMP_FORMAT_MEMORY_ENC:
                        startOp("xmlDocDumpFormatMemoryEnc"); break;
                    case OP_HTML_DOC_DUMP_MEMORY:
                        startOp("htmlDocDumpMemory"); break;
                    case OP_HTML_DOC_DUMP_MEMORY_FORMAT:
                        startOp("htmlDocDumpMemoryFormat"); break;
                }

                incStrIdx();
                doc = getDoc(0);

                switch (op) {
                    case OP_XML_DOC_DUMP_MEMORY:
                        xmlDocDumpMemory(doc, &out, &outSize);
                        break;
                    case OP_XML_DOC_DUMP_MEMORY_ENC:
                        xmlDocDumpMemoryEnc(doc, &out, &outSize,
                            (const char *) getStr(1));
                        break;
                    case OP_XML_DOC_DUMP_FORMAT_MEMORY:
                        xmlDocDumpFormatMemory(doc, &out, &outSize,
                            getInt(0));
                        break;
                    case OP_XML_DOC_DUMP_FORMAT_MEMORY_ENC:
                        xmlDocDumpFormatMemoryEnc(doc, &out, &outSize,
                            (const char *) getStr(1),
                            getInt(0));
                        break;
#ifdef LIBXML_HTML_ENABLED
                    case OP_HTML_DOC_DUMP_MEMORY:
                        htmlDocDumpMemory(doc, &out, &outSize);
                        break;
                    case OP_HTML_DOC_DUMP_MEMORY_FORMAT:
                        htmlDocDumpMemoryFormat(doc, &out, &outSize,
                            getInt(0));
                        break;
#endif /* LIBXML_HTML_ENABLED */
                }

                /* Could be an unknown encoding */
                if (out != NULL)
                    oomReport = 0;
                moveStr(0, out);
                endOp();
                break;
            }

            case OP_XML_NODE_DUMP:
            case OP_XML_NODE_BUF_GET_CONTENT:
            case OP_XML_ATTR_SERIALIZE_TXT_CONTENT:
            case OP_XML_DUMP_ELEMENT_DECL:
            case OP_XML_DUMP_ELEMENT_TABLE:
            case OP_XML_DUMP_ATTRIBUTE_DECL:
            case OP_XML_DUMP_ATTRIBUTE_TABLE:
            case OP_XML_DUMP_ENTITY_DECL:
            case OP_XML_DUMP_ENTITIES_TABLE:
            case OP_XML_DUMP_NOTATION_DECL:
            case OP_XML_DUMP_NOTATION_TABLE:
            case OP_HTML_NODE_DUMP: {
                xmlNodePtr node;
                xmlDocPtr doc;
                xmlBufferPtr buffer;
                xmlChar *dump;
                int level, format, res;

                switch (op) {
                    case OP_XML_NODE_DUMP:
                        startOp("xmlNodeDump"); break;
                    case OP_XML_NODE_BUF_GET_CONTENT:
                        startOp("xmlNodeBufGetContent"); break;
                    case OP_XML_ATTR_SERIALIZE_TXT_CONTENT:
                        startOp("xmlAttrSerializeTxtContent"); break;
                    case OP_XML_DUMP_ELEMENT_DECL:
                        startOp("xmlDumpElementDecl"); break;
                    case OP_XML_DUMP_ELEMENT_TABLE:
                        startOp("xmlDumpElementTable"); break;
                    case OP_XML_DUMP_ATTRIBUTE_DECL:
                        startOp("xmlDumpAttributeDecl"); break;
                    case OP_XML_DUMP_ATTRIBUTE_TABLE:
                        startOp("xmlDumpAttributeTable"); break;
                    case OP_XML_DUMP_ENTITY_DECL:
                        startOp("xmlDumpEntityDecl"); break;
                    case OP_XML_DUMP_ENTITIES_TABLE:
                        startOp("xmlDumpEntitiesTable"); break;
                    case OP_XML_DUMP_NOTATION_DECL:
                        startOp("xmlDumpNotationDecl"); break;
                    case OP_XML_DUMP_NOTATION_TABLE:
                        startOp("xmlDumpNotationTable"); break;
                    case OP_HTML_NODE_DUMP:
                        startOp("htmlNodeDump"); break;
                }

                incStrIdx();
                buffer = xmlBufferCreate();
                xmlFuzzResetMallocFailed();
                node = getNode(0);
                doc = node ? node->doc : NULL;
                level = getInt(0);
                format = getInt(0);
                res = 0;

                switch (op) {
                    case OP_XML_NODE_DUMP:
                        res = xmlNodeDump(buffer, doc, node, level, format);
                        break;
                    case OP_XML_NODE_BUF_GET_CONTENT:
                        res = xmlNodeBufGetContent(buffer, node);
                        break;
                    case OP_XML_ATTR_SERIALIZE_TXT_CONTENT:
                        if (node != NULL && node->type != XML_ATTRIBUTE_NODE)
                            node = NULL;
                        xmlAttrSerializeTxtContent(
                            buffer, doc,
                            (xmlAttrPtr) node,
                            getStr(1));
                        break;
                    case OP_XML_DUMP_ELEMENT_DECL:
                        if (node != NULL && node->type != XML_ELEMENT_DECL)
                            node = NULL;
                        xmlDumpElementDecl(buffer, (xmlElementPtr) node);
                        break;
                    case OP_XML_DUMP_ATTRIBUTE_DECL:
                        if (node != NULL && node->type != XML_ATTRIBUTE_DECL)
                            node = NULL;
                        xmlDumpAttributeDecl(buffer, (xmlAttributePtr) node);
                        break;
                    case OP_XML_DUMP_NOTATION_DECL:
                        /* TODO */
                        break;
                    case OP_XML_DUMP_ENTITY_DECL:
                        if (node != NULL && node->type != XML_ENTITY_DECL)
                            node = NULL;
                        xmlDumpEntityDecl(buffer, (xmlEntityPtr) node);
                        break;
                    case OP_XML_DUMP_ELEMENT_TABLE: {
                        xmlElementTablePtr table;

                        table = node != NULL && node->type == XML_DTD_NODE ?
                                ((xmlDtdPtr) node)->elements :
                                NULL;
                        xmlDumpElementTable(buffer, table);
                        break;
                    }
                    case OP_XML_DUMP_ATTRIBUTE_TABLE: {
                        xmlAttributeTablePtr table;

                        table = node != NULL && node->type == XML_DTD_NODE ?
                                ((xmlDtdPtr) node)->attributes :
                                NULL;
                        xmlDumpAttributeTable(buffer, table);
                        break;
                    }
                    case OP_XML_DUMP_NOTATION_TABLE: {
                        xmlNotationTablePtr table;

                        table = node != NULL && node->type == XML_DTD_NODE ?
                                ((xmlDtdPtr) node)->notations :
                                NULL;
                        xmlDumpNotationTable(buffer, table);
                        break;
                    }
                    case OP_XML_DUMP_ENTITIES_TABLE: {
                        xmlEntitiesTablePtr table;

                        table = node != NULL && node->type == XML_DTD_NODE ?
                                ((xmlDtdPtr) node)->entities :
                                NULL;
                        xmlDumpEntitiesTable(buffer, table);
                        break;
                    }
#ifdef LIBXML_HTML_ENABLED
                    case OP_HTML_NODE_DUMP:
                        res = htmlNodeDump(buffer, doc, node);
                        break;
#endif /* LIBXML_HTML_ENABLED */
                }

                dump = xmlBufferDetach(buffer);
                if (res == 0 && dump != NULL)
                    oomReport = 0;
                moveStr(0, dump);
                xmlBufferFree(buffer);
                endOp();
                break;
            }

            case OP_XML_SAVE_FILE_TO:
            case OP_XML_SAVE_FORMAT_FILE_TO:
            case OP_XML_NODE_DUMP_OUTPUT:
            case OP_HTML_DOC_CONTENT_DUMP_OUTPUT:
            case OP_HTML_DOC_CONTENT_DUMP_FORMAT_OUTPUT:
            case OP_HTML_NODE_DUMP_OUTPUT:
            case OP_HTML_NODE_DUMP_FORMAT_OUTPUT: {
                xmlNodePtr node;
                xmlDocPtr doc;
                xmlOutputBufferPtr output;
                const char *encoding;
                int level, format, argsOk, res, closed;

                switch (op) {
                    case OP_XML_SAVE_FILE_TO:
                        startOp("xmlSaveFileTo"); break;
                    case OP_XML_SAVE_FORMAT_FILE_TO:
                        startOp("xmlSaveFormatFileTo"); break;
                    case OP_XML_NODE_DUMP_OUTPUT:
                        startOp("xmlNodeDumpOutput"); break;
                    case OP_HTML_DOC_CONTENT_DUMP_OUTPUT:
                        startOp("htmlDocContentDumpOutput"); break;
                    case OP_HTML_DOC_CONTENT_DUMP_FORMAT_OUTPUT:
                        startOp("htmlDocContentDumpFormatOutput"); break;
                    case OP_HTML_NODE_DUMP_OUTPUT:
                        startOp("htmlNodeDumpOutput"); break;
                    case OP_HTML_NODE_DUMP_FORMAT_OUTPUT:
                        startOp("htmlNodeDumpFormatOutput"); break;
                }

                incStrIdx();
                output = xmlAllocOutputBuffer(NULL);
                xmlFuzzResetMallocFailed();
                node = getNode(0);
                doc = node ? node->doc : NULL;
                encoding = (const char *) getStr(1);
                level = getInt(0);
                format = getInt(0);
                argsOk = (output != NULL);
                res = 0;
                closed = 0;

                switch (op) {
                    case OP_XML_SAVE_FILE_TO:
                        argsOk &= (doc != NULL);
                        res = xmlSaveFileTo(output, doc, encoding);
                        closed = 1;
                        break;
                    case OP_XML_SAVE_FORMAT_FILE_TO:
                        argsOk &= (doc != NULL);
                        res = xmlSaveFormatFileTo(output, doc, encoding, format);
                        closed = 1;
                        break;
                    case OP_XML_NODE_DUMP_OUTPUT:
                        argsOk &= (node != NULL);
                        xmlNodeDumpOutput(output, doc, node, level, format,
                                          encoding);
                        break;
#ifdef LIBXML_HTML_ENABLED
                    case OP_HTML_DOC_CONTENT_DUMP_OUTPUT:
                        argsOk &= (doc != NULL);
                        htmlDocContentDumpOutput(output, doc, encoding);
                        break;
                    case OP_HTML_DOC_CONTENT_DUMP_FORMAT_OUTPUT:
                        argsOk &= (doc != NULL);
                        htmlDocContentDumpFormatOutput(output, doc, encoding,
                                                       format);
                        break;
                    case OP_HTML_NODE_DUMP_OUTPUT:
                        argsOk &= (node != NULL);
                        htmlNodeDumpOutput(output, doc, node, encoding);
                        break;
                    case OP_HTML_NODE_DUMP_FORMAT_OUTPUT:
                        argsOk &= (node != NULL);
                        htmlNodeDumpFormatOutput(output, doc, node, encoding,
                                                 format);
                        break;
#endif /* LIBXML_HTML_ENABLED */
                }

                if (closed) {
                    if (res >= 0)
                        oomReport = 0;
                    moveStr(0, NULL);
                } else {
                    oomReport =
                        (output != NULL &&
                         output->error == XML_ERR_NO_MEMORY);
                    if (argsOk && !output->error)
                        copyStr(0, xmlBufContent(output->buffer));
                    else
                        moveStr(0, NULL);
                    xmlOutputBufferClose(output);
                }
                endOp();
                break;
            }
#endif /* LIBXML_OUTPUT_ENABLED */

            case OP_XML_DOM_WRAP_RECONCILE_NAMESPACES: {
                xmlNodePtr node;
                int res;

                startOp("xmlDOMWrapReconcileNamespaces");
                res = xmlDOMWrapReconcileNamespaces(
                    NULL,
                    node = getNode(0),
                    getInt(0));
                oomReport =
                    (node != NULL &&
                     node->doc != NULL &&
                     node->type == XML_ELEMENT_NODE &&
                     res < 0);
                endOp();
                break;
            }

            case OP_XML_DOM_WRAP_ADOPT_NODE: {
                xmlDOMWrapCtxtPtr ctxt;
                xmlDocPtr doc, destDoc, oldDoc;
                xmlNodePtr node, destParent, oldParent;
                int res;

                startOp("xmlDOMWrapAdoptNode");
                ctxt = xmlDOMWrapNewCtxt();
                doc = getDoc(0);
                node = getNode(1);
                destDoc = getDoc(2);
                destParent = getNode(3);

                if (!isValidChild(destParent, node))
                    destParent = NULL;

                oldParent = node ? node->parent : NULL;
                oldDoc = node ? node->doc : NULL;

                res = xmlDOMWrapAdoptNode(
                    ctxt,
                    doc,
                    node,
                    destDoc,
                    destParent,
                    getInt(0));
                if (ctxt == NULL)
                    oomReport = 1;
                else if (res == 0)
                    oomReport = 0;

                if (node != NULL) {
                    /* Node can reference destParent's namespaces */
                    if (destParent != NULL &&
                        node->parent == NULL &&
                        node->doc == destParent->doc) {
                        if (node->type == XML_ATTRIBUTE_NODE) {
                            xmlNodePtr prop;

                            /* Insert without removing duplicates */
                            node->parent = destParent;
                            prop = (xmlNodePtr) destParent->properties;
                            node->next = prop;
                            if (prop != NULL)
                                prop->prev = node;
                            destParent->properties = (xmlAttrPtr) node;
                        } else if (node->type != XML_TEXT_NODE) {
                            xmlAddChild(destParent, node);
                        }
                    }

                    /* Node can be unlinked and moved to a new document. */
                    if (oldParent != NULL && node->parent != oldParent)
                        dropNode(oldParent);
                    else if (node->doc != oldDoc)
                        dropNode((xmlNodePtr) oldDoc);
                }

                xmlDOMWrapFreeCtxt(ctxt);
                endOp();
                break;
            }

            case OP_XML_DOM_WRAP_REMOVE_NODE: {
                xmlDocPtr doc;
                xmlNodePtr node, oldParent;
                int res;

                startOp("xmlDOMWrapRemoveNode");
                doc = getDoc(0);
                node = getNode(1);
                oldParent = node ? node->parent : NULL;
                res = xmlDOMWrapRemoveNode(NULL, doc, node, 0);
                oomReport =
                    (node != NULL &&
                     doc != NULL &&
                     node->doc == doc &&
                     res < 0);
                if (node && node->parent != oldParent)
                    dropNode(oldParent);
                endOp();
                break;
            }

            case OP_XML_DOM_WRAP_CLONE_NODE: {
                xmlDOMWrapCtxtPtr ctxt;
                xmlDocPtr doc, destDoc;
                xmlNodePtr node, destParent, copy = NULL;
                int res;

                startOp("xmlDOMWrapCloneNode");
                incNodeIdx();
                ctxt = xmlDOMWrapNewCtxt();
                doc = getDoc(1);
                node = getNode(2);
                destDoc = getDoc(3);
                destParent = getNode(4);

                if (destParent != NULL &&
                    node != NULL &&
                    !isValidChildType(destParent, node->type))
                    destParent = NULL;

                /* xmlDOMWrapCloneNode returns a garbage node on error. */
                res = xmlDOMWrapCloneNode(
                    ctxt,
                    doc,
                    node,
                    &copy,
                    destDoc,
                    destParent,
                    getInt(0),
                    0);
                if (ctxt == NULL)
                    oomReport = 1;
                else if (res == 0)
                    oomReport = 0;
                copy = checkCopy(copy);

                /* Copy can reference destParent's namespaces */
                if (destParent != NULL && copy != NULL) {
                    if (copy->type == XML_ATTRIBUTE_NODE) {
                        xmlNodePtr prop;

                        /* Insert without removing duplicates */
                        copy->parent = destParent;
                        prop = (xmlNodePtr) destParent->properties;
                        copy->next = prop;
                        if (prop != NULL)
                            prop->prev = copy;
                        destParent->properties = (xmlAttrPtr) copy;
                    } else if (copy->type != XML_TEXT_NODE) {
                        xmlAddChild(destParent, copy);
                    }
                }

                xmlDOMWrapFreeCtxt(ctxt);
                setNode(0, copy);
                break;
            }

            case OP_XML_CHILD_ELEMENT_COUNT:
                startOp("xmlChildElementCount");
                incIntIdx();
                setInt(0, xmlChildElementCount(getNode(0)));
                oomReport = 0;
                break;

            case OP_XML_FIRST_ELEMENT_CHILD:
                startOp("xmlFirstElementChild");
                incNodeIdx();
                setNode(0, xmlFirstElementChild(getNode(1)));
                oomReport = 0;
                break;

            case OP_XML_LAST_ELEMENT_CHILD:
                startOp("xmlLastElementChild");
                incNodeIdx();
                setNode(0, xmlLastElementChild(getNode(1)));
                oomReport = 0;
                break;

            case OP_XML_NEXT_ELEMENT_SIBLING:
                startOp("xmlNextElementSibling");
                incNodeIdx();
                setNode(0, xmlNextElementSibling(getNode(1)));
                oomReport = 0;
                break;

            case OP_XML_PREVIOUS_ELEMENT_SIBLING:
                startOp("xmlPreviousElementSibling");
                incNodeIdx();
                setNode(0, xmlPreviousElementSibling(getNode(1)));
                oomReport = 0;
                break;

            default:
                break;
        }

        xmlFuzzCheckMallocFailure(vars->opName, oomReport);
    }

    for (i = 0; i < REG_MAX; i++)
        xmlFree(vars->strings[i]);

    for (i = 0; i < REG_MAX; i++) {
        xmlNodePtr node = vars->nodes[i];

        vars->nodes[i] = NULL;
        dropNode(node);
    }

    xmlFuzzMemSetLimit(0);
    xmlFuzzDataCleanup();
    xmlResetLastError();
    return(0);
}

