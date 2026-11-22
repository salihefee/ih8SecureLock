import android.os.Binder;
import android.os.Build;
import android.os.Parcel;
import android.view.WindowManager;

public class ParcelHookCheck {
    static native void init();
    static native int code(int kind);
    static native int invoke(int code, Parcel data, Parcel reply, int flags);
    static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    static final String WINDOW = "android.view.IWindowSession";
    static void window(int transaction, boolean secure, boolean present, String descriptor) {
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(descriptor);
            Binder token = new Binder();
            int binderPosition = data.dataPosition();
            data.writeStrongBinder(token);
            if (Build.VERSION.SDK_INT <= 30) data.writeInt(7);
            data.writeInt(present ? 1 : 0);
            int attrsPosition = data.dataPosition();
            WindowManager.LayoutParams attrs = new WindowManager.LayoutParams();
            attrs.flags = 0x100 | (secure ? WindowManager.LayoutParams.FLAG_SECURE : 0);
            if (present) {
                attrs.writeToParcel(data, 0);
                int end = data.dataPosition();
                data.setDataPosition(attrsPosition + 24);
                data.writeInt(0x2000); // privateFlags immediately follows flags.
                data.setDataPosition(end);
            }
            int size = data.dataSize(), position = data.dataPosition();
            check(invoke(transaction, data, reply, 0) == 1, "must forward window request");
            check(data.dataSize() == size && data.dataPosition() == position, "size/cursor changed");
            data.setDataPosition(binderPosition);
            check(data.readStrongBinder() == token, "Binder object table lost");
            if (present) {
                data.setDataPosition(attrsPosition);
                WindowManager.LayoutParams result = WindowManager.LayoutParams.CREATOR.createFromParcel(data);
                int expected = WINDOW.equals(descriptor) ? 0x100 : attrs.flags;
                check(result.flags == expected, "wrong flags: " + result.flags);
                data.setDataPosition(attrsPosition + 24);
                check(data.readInt() == 0x2000, "privateFlags changed");
            }
        } finally { data.recycle(); reply.recycle(); }
    }
    public static void main(String[] args) {
        System.load(args[0]);
        init();
        int tested = 0;
        for (int kind = 0; kind < 4; ++kind) {
            int transaction = code(kind);
            if (transaction == 0) continue;
            window(transaction, true, true, WINDOW);
            window(transaction, false, true, WINDOW);
            window(transaction, true, false, WINDOW);
            window(transaction, true, true, "other.window.Interface");
            ++tested;
        }
        check(tested > 0, "no relayout transactions");
        if (code(4) != 0) {
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            data.writeInterfaceToken("android.app.IActivityTaskManager");
            data.writeStrongBinder(new Binder());
            data.writeStrongBinder(new Binder());
            int position = data.dataPosition();
            check(invoke(code(4), data, reply, 0) == 0, "observer registration forwarded");
            check(data.dataPosition() == position && reply.dataPosition() == 0, "observer cursor changed");
            reply.readException();
            check(invoke(code(4), data, null, 1) == 0, "oneway observer registration forwarded");
            data.recycle(); reply.recycle();
        }
        Parcel truncated = Parcel.obtain();
        truncated.writeInt(0);
        check(invoke(code(0), truncated, null, 0) == 1, "truncated request not forwarded");
        check(truncated.dataPosition() == 4, "truncated cursor changed");
        truncated.recycle();
        System.out.println("PASS: " + tested + " relayout variants; real LayoutParams, Binder objects, null attrs, unrelated interfaces, malformed requests, observer reply and cursor preservation");
    }
}
