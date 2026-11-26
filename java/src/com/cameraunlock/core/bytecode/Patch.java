// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
package com.cameraunlock.core.bytecode;

import java.lang.classfile.ClassFile;
import java.lang.classfile.ClassHierarchyResolver;
import java.lang.classfile.ClassModel;
import java.lang.classfile.ClassTransform;
import java.lang.classfile.CodeBuilder;
import java.lang.classfile.CodeElement;
import java.lang.classfile.CodeTransform;
import java.lang.classfile.Label;
import java.lang.classfile.MethodModel;
import java.lang.classfile.instruction.InvokeInstruction;
import java.lang.classfile.instruction.ReturnInstruction;
import java.lang.constant.ClassDesc;
import java.lang.constant.ConstantDescs;
import java.lang.constant.MethodTypeDesc;
import java.lang.invoke.MethodType;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * One call into the mod's bridge, {@code static boolean call(int site, Object a, Object b)} on the
 * class {@link #apply} is given, written into a game method: after a named call inside it, before
 * each of its returns, or at its start, where a true answer returns from the method at once.
 * {@code a} and {@code b} are the local slots handed over, each holding a reference (0 is
 * {@code this} in an instance method), or -1 for null.
 *
 * The bridge class is the mod's own, and has to be visible to the game class's loader (on the boot
 * class path, say), since the patched code resolves it from there.
 */
public final class Patch {
    public enum Kind { AFTER_CALL, BEFORE_RETURN, SKIP_AT_START }

    private static final MethodTypeDesc CALL = MethodTypeDesc.of(ConstantDescs.CD_boolean, ConstantDescs.CD_int,
            ConstantDescs.CD_Object, ConstantDescs.CD_Object);

    public final Kind kind;
    public final int site;
    public final Method target;
    /** The call AFTER_CALL goes after; null for the other kinds. */
    public final Method callee;
    public final int a;
    public final int b;
    private final String targetDescriptor;
    private final String calleeOwner, calleeDescriptor;

    public Patch(Kind kind, int site, Method target, Method callee, int a, int b) {
        Objects.requireNonNull(kind, "kind");
        Objects.requireNonNull(target, "target");
        if (kind == Kind.AFTER_CALL && callee == null) throw new IllegalArgumentException("an AFTER_CALL patch needs the callee it goes after");
        if (kind != Kind.AFTER_CALL && callee != null) throw new IllegalArgumentException("only an AFTER_CALL patch takes a callee, not " + kind);
        if (kind == Kind.SKIP_AT_START && target.getReturnType() != void.class) {
            throw new IllegalArgumentException("a SKIP_AT_START patch returns nothing, so it cannot go into " + target
                    + ", which returns " + target.getReturnType().getName());
        }
        this.kind = kind;
        this.site = site;
        this.target = target;
        this.callee = callee;
        this.a = a;
        this.b = b;
        targetDescriptor = descriptor(target);
        calleeOwner = callee == null ? null : internalName(callee.getDeclaringClass());
        calleeDescriptor = callee == null ? null : descriptor(callee);
    }

    private static String descriptor(Method m) {
        return MethodType.methodType(m.getReturnType(), m.getParameterTypes()).toMethodDescriptorString();
    }

    /** The class's name as a class file writes it, with slashes, which is also how a ClassFileTransformer is handed it. */
    public static String internalName(Class<?> c) {
        return c.getName().replace('.', '/');
    }

    private boolean targets(MethodModel mm) {
        return mm.methodName().equalsString(target.getName()) && mm.methodType().equalsString(targetDescriptor);
    }

    private void emit(CodeBuilder code, ClassDesc bridge) {
        code.loadConstant(site);
        load(code, a);
        load(code, b);
        code.invokestatic(bridge, "call", CALL);
    }

    private static void load(CodeBuilder code, int slot) {
        if (slot < 0) code.aconst_null();
        else code.aload(slot);
    }

    private CodeTransform code(ClassDesc bridge) {
        return new CodeTransform() {
            int matched;

            @Override
            public void atStart(CodeBuilder code) {
                if (kind != Kind.SKIP_AT_START) return;
                Label go = code.newLabel();
                emit(code, bridge);
                code.ifeq(go);
                code.return_();
                code.labelBinding(go);
                matched++;
            }

            @Override
            public void accept(CodeBuilder code, CodeElement element) {
                if (kind == Kind.BEFORE_RETURN && element instanceof ReturnInstruction) {
                    emit(code, bridge);
                    code.pop();
                    matched++;
                }
                code.with(element);
                if (kind == Kind.AFTER_CALL && element instanceof InvokeInstruction call
                        && call.owner().asInternalName().equals(calleeOwner)
                        && call.name().equalsString(callee.getName())
                        && call.type().equalsString(calleeDescriptor)) {
                    emit(code, bridge);
                    code.pop();
                    matched++;
                }
            }

            @Override
            public void atEnd(CodeBuilder code) {
                if (matched == 0) {
                    throw new IllegalStateException(describe() + " found nowhere to go in the method's code");
                }
            }
        };
    }

    /** Where the patch goes, in words, for a log line. */
    public String describe() {
        String where = switch (kind) {
            case AFTER_CALL -> "after its call to " + callee.getName();
            case BEFORE_RETURN -> "before each return";
            case SKIP_AT_START -> "at its start";
        };
        return target.getDeclaringClass().getName() + "." + target.getName() + " " + where;
    }

    /**
     * {@code bytes}, one class's file, with every patch for that class written in, each calling
     * {@code bridge} (a binary class name, such as {@code mymod.bridge.Bridge}). The class hierarchy
     * the stack maps need is read through {@code loader}. Throws IllegalArgumentException when a
     * patch's target is not declared in this class, and IllegalStateException naming the patch when
     * one finds nowhere to go.
     */
    public static byte[] apply(byte[] bytes, List<Patch> patches, ClassLoader loader, String bridge) {
        ClassDesc bridgeClass = ClassDesc.of(bridge);
        ClassFile cf = ClassFile.of(ClassFile.ClassHierarchyResolverOption.of(
                ClassHierarchyResolver.ofResourceParsing(loader).orElse(ClassHierarchyResolver.defaultResolver())));
        ClassModel model = cf.parse(bytes);
        String own = model.thisClass().asInternalName();
        for (Patch p : patches) {
            if (!internalName(p.target.getDeclaringClass()).equals(own)) {
                throw new IllegalArgumentException(p.describe() + " is not a patch for " + own.replace('/', '.'));
            }
        }
        // A skip's early return is written last, so no return hook lands before it.
        List<Patch> ordered = new ArrayList<>(patches);
        ordered.sort((x, y) -> x.kind.compareTo(y.kind));
        ClassTransform transform = ClassTransform.ACCEPT_ALL;
        for (Patch p : ordered) transform = transform.andThen(ClassTransform.transformingMethodBodies(p::targets, p.code(bridgeClass)));
        return cf.transformClass(model, transform);
    }
}
